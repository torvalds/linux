/*
 * Hardware driver for DAQ-STC based boards
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2001 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2002-2006 Frank Mori Hess <fmhess@users.sourceforge.net>
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
 * This file is meant to be included by another file, e.g.,
 * ni_atmio.c or ni_pcimio.c.
 *
 * Interrupt support originally added by Truxton Fulton <trux@truxton.com>
 *
 * References (ftp://ftp.natinst.com/support/manuals):
 *   340747b.pdf  AT-MIO E series Register Level Programmer Manual
 *   341079b.pdf  PCI E Series RLPM
 *   340934b.pdf  DAQ-STC reference manual
 *
 * 67xx and 611x registers (ftp://ftp.ni.com/support/daq/mhddk/documentation/)
 *   release_ni611x.pdf
 *   release_ni67xx.pdf
 *
 * Other possibly relevant info:
 *   320517c.pdf  User manual (obsolete)
 *   320517f.pdf  User manual (new)
 *   320889a.pdf  delete
 *   320906c.pdf  maximum signal ratings
 *   321066a.pdf  about 16x
 *   321791a.pdf  discontinuation of at-mio-16e-10 rev. c
 *   321808a.pdf  about at-mio-16e-10 rev P
 *   321837a.pdf  discontinuation of at-mio-16de-10 rev d
 *   321838a.pdf  about at-mio-16de-10 rev N
 *
 * ISSUES:
 *   - the interrupt routine needs to be cleaned up
 *
 * 2006-02-07: S-Series PCI-6143: Support has been added but is not
 * fully tested as yet. Terry Barnaby, BEAM Ltd.
 */

#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include "8255.h"
#include "mite.h"

/* A timeout count */
#define NI_TIMEOUT 1000

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

#define NI_GPCT_SUBDEV(x)	(NI_GPCT0_SUBDEV + (x))

enum timebase_nanoseconds {
	TIMEBASE_1_NS = 50,
	TIMEBASE_2_NS = 10000
};

#define SERIAL_DISABLED		0
#define SERIAL_600NS		600
#define SERIAL_1_2US		1200
#define SERIAL_10US			10000

static const int num_adc_stages_611x = 3;

static void ni_writel(struct comedi_device *dev, unsigned int data, int reg)
{
	if (dev->mmio)
		writel(data, dev->mmio + reg);
	else
		outl(data, dev->iobase + reg);
}

static void ni_writew(struct comedi_device *dev, unsigned int data, int reg)
{
	if (dev->mmio)
		writew(data, dev->mmio + reg);
	else
		outw(data, dev->iobase + reg);
}

static void ni_writeb(struct comedi_device *dev, unsigned int data, int reg)
{
	if (dev->mmio)
		writeb(data, dev->mmio + reg);
	else
		outb(data, dev->iobase + reg);
}

static unsigned int ni_readl(struct comedi_device *dev, int reg)
{
	if (dev->mmio)
		return readl(dev->mmio + reg);

	return inl(dev->iobase + reg);
}

static unsigned int ni_readw(struct comedi_device *dev, int reg)
{
	if (dev->mmio)
		return readw(dev->mmio + reg);

	return inw(dev->iobase + reg);
}

static unsigned int ni_readb(struct comedi_device *dev, int reg)
{
	if (dev->mmio)
		return readb(dev->mmio + reg);

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

struct mio_regmap {
	unsigned int mio_reg;
	int size;
};

static const struct mio_regmap m_series_stc_write_regmap[] = {
	[NISTC_INTA_ACK_REG]		= { 0x104, 2 },
	[NISTC_INTB_ACK_REG]		= { 0x106, 2 },
	[NISTC_AI_CMD2_REG]		= { 0x108, 2 },
	[NISTC_AO_CMD2_REG]		= { 0x10a, 2 },
	[NISTC_G0_CMD_REG]		= { 0x10c, 2 },
	[NISTC_G1_CMD_REG]		= { 0x10e, 2 },
	[NISTC_AI_CMD1_REG]		= { 0x110, 2 },
	[NISTC_AO_CMD1_REG]		= { 0x112, 2 },
	/*
	 * NISTC_DIO_OUT_REG maps to:
	 * { NI_M_DIO_REG, 4 } and { NI_M_SCXI_SER_DO_REG, 1 }
	 */
	[NISTC_DIO_OUT_REG]		= { 0, 0 }, /* DOES NOT MAP CLEANLY */
	[NISTC_DIO_CTRL_REG]		= { 0, 0 }, /* DOES NOT MAP CLEANLY */
	[NISTC_AI_MODE1_REG]		= { 0x118, 2 },
	[NISTC_AI_MODE2_REG]		= { 0x11a, 2 },
	[NISTC_AI_SI_LOADA_REG]		= { 0x11c, 4 },
	[NISTC_AI_SI_LOADB_REG]		= { 0x120, 4 },
	[NISTC_AI_SC_LOADA_REG]		= { 0x124, 4 },
	[NISTC_AI_SC_LOADB_REG]		= { 0x128, 4 },
	[NISTC_AI_SI2_LOADA_REG]	= { 0x12c, 4 },
	[NISTC_AI_SI2_LOADB_REG]	= { 0x130, 4 },
	[NISTC_G0_MODE_REG]		= { 0x134, 2 },
	[NISTC_G1_MODE_REG]		= { 0x136, 2 },
	[NISTC_G0_LOADA_REG]		= { 0x138, 4 },
	[NISTC_G0_LOADB_REG]		= { 0x13c, 4 },
	[NISTC_G1_LOADA_REG]		= { 0x140, 4 },
	[NISTC_G1_LOADB_REG]		= { 0x144, 4 },
	[NISTC_G0_INPUT_SEL_REG]	= { 0x148, 2 },
	[NISTC_G1_INPUT_SEL_REG]	= { 0x14a, 2 },
	[NISTC_AO_MODE1_REG]		= { 0x14c, 2 },
	[NISTC_AO_MODE2_REG]		= { 0x14e, 2 },
	[NISTC_AO_UI_LOADA_REG]		= { 0x150, 4 },
	[NISTC_AO_UI_LOADB_REG]		= { 0x154, 4 },
	[NISTC_AO_BC_LOADA_REG]		= { 0x158, 4 },
	[NISTC_AO_BC_LOADB_REG]		= { 0x15c, 4 },
	[NISTC_AO_UC_LOADA_REG]		= { 0x160, 4 },
	[NISTC_AO_UC_LOADB_REG]		= { 0x164, 4 },
	[NISTC_CLK_FOUT_REG]		= { 0x170, 2 },
	[NISTC_IO_BIDIR_PIN_REG]	= { 0x172, 2 },
	[NISTC_RTSI_TRIG_DIR_REG]	= { 0x174, 2 },
	[NISTC_INT_CTRL_REG]		= { 0x176, 2 },
	[NISTC_AI_OUT_CTRL_REG]		= { 0x178, 2 },
	[NISTC_ATRIG_ETC_REG]		= { 0x17a, 2 },
	[NISTC_AI_START_STOP_REG]	= { 0x17c, 2 },
	[NISTC_AI_TRIG_SEL_REG]		= { 0x17e, 2 },
	[NISTC_AI_DIV_LOADA_REG]	= { 0x180, 4 },
	[NISTC_AO_START_SEL_REG]	= { 0x184, 2 },
	[NISTC_AO_TRIG_SEL_REG]		= { 0x186, 2 },
	[NISTC_G0_AUTOINC_REG]		= { 0x188, 2 },
	[NISTC_G1_AUTOINC_REG]		= { 0x18a, 2 },
	[NISTC_AO_MODE3_REG]		= { 0x18c, 2 },
	[NISTC_RESET_REG]		= { 0x190, 2 },
	[NISTC_INTA_ENA_REG]		= { 0x192, 2 },
	[NISTC_INTA2_ENA_REG]		= { 0, 0 }, /* E-Series only */
	[NISTC_INTB_ENA_REG]		= { 0x196, 2 },
	[NISTC_INTB2_ENA_REG]		= { 0, 0 }, /* E-Series only */
	[NISTC_AI_PERSONAL_REG]		= { 0x19a, 2 },
	[NISTC_AO_PERSONAL_REG]		= { 0x19c, 2 },
	[NISTC_RTSI_TRIGA_OUT_REG]	= { 0x19e, 2 },
	[NISTC_RTSI_TRIGB_OUT_REG]	= { 0x1a0, 2 },
	[NISTC_RTSI_BOARD_REG]		= { 0, 0 }, /* Unknown */
	[NISTC_CFG_MEM_CLR_REG]		= { 0x1a4, 2 },
	[NISTC_ADC_FIFO_CLR_REG]	= { 0x1a6, 2 },
	[NISTC_DAC_FIFO_CLR_REG]	= { 0x1a8, 2 },
	[NISTC_AO_OUT_CTRL_REG]		= { 0x1ac, 2 },
	[NISTC_AI_MODE3_REG]		= { 0x1ae, 2 },
};

static void m_series_stc_write(struct comedi_device *dev,
			       unsigned int data, unsigned int reg)
{
	const struct mio_regmap *regmap;

	if (reg < ARRAY_SIZE(m_series_stc_write_regmap)) {
		regmap = &m_series_stc_write_regmap[reg];
	} else {
		dev_warn(dev->class_dev, "%s: unhandled register=0x%x\n",
			 __func__, reg);
		return;
	}

	switch (regmap->size) {
	case 4:
		ni_writel(dev, data, regmap->mio_reg);
		break;
	case 2:
		ni_writew(dev, data, regmap->mio_reg);
		break;
	default:
		dev_warn(dev->class_dev, "%s: unmapped register=0x%x\n",
			 __func__, reg);
		break;
	}
}

static const struct mio_regmap m_series_stc_read_regmap[] = {
	[NISTC_AI_STATUS1_REG]		= { 0x104, 2 },
	[NISTC_AO_STATUS1_REG]		= { 0x106, 2 },
	[NISTC_G01_STATUS_REG]		= { 0x108, 2 },
	[NISTC_AI_STATUS2_REG]		= { 0, 0 }, /* Unknown */
	[NISTC_AO_STATUS2_REG]		= { 0x10c, 2 },
	[NISTC_DIO_IN_REG]		= { 0, 0 }, /* Unknown */
	[NISTC_G0_HW_SAVE_REG]		= { 0x110, 4 },
	[NISTC_G1_HW_SAVE_REG]		= { 0x114, 4 },
	[NISTC_G0_SAVE_REG]		= { 0x118, 4 },
	[NISTC_G1_SAVE_REG]		= { 0x11c, 4 },
	[NISTC_AO_UI_SAVE_REG]		= { 0x120, 4 },
	[NISTC_AO_BC_SAVE_REG]		= { 0x124, 4 },
	[NISTC_AO_UC_SAVE_REG]		= { 0x128, 4 },
	[NISTC_STATUS1_REG]		= { 0x136, 2 },
	[NISTC_DIO_SERIAL_IN_REG]	= { 0x009, 1 },
	[NISTC_STATUS2_REG]		= { 0x13a, 2 },
	[NISTC_AI_SI_SAVE_REG]		= { 0x180, 4 },
	[NISTC_AI_SC_SAVE_REG]		= { 0x184, 4 },
};

static unsigned int m_series_stc_read(struct comedi_device *dev,
				      unsigned int reg)
{
	const struct mio_regmap *regmap;

	if (reg < ARRAY_SIZE(m_series_stc_read_regmap)) {
		regmap = &m_series_stc_read_regmap[reg];
	} else {
		dev_warn(dev->class_dev, "%s: unhandled register=0x%x\n",
			 __func__, reg);
		return 0;
	}

	switch (regmap->size) {
	case 4:
		return ni_readl(dev, regmap->mio_reg);
	case 2:
		return ni_readw(dev, regmap->mio_reg);
	case 1:
		return ni_readb(dev, regmap->mio_reg);
	default:
		dev_warn(dev->class_dev, "%s: unmapped register=0x%x\n",
			 __func__, reg);
		return 0;
	}
}

static void ni_stc_writew(struct comedi_device *dev,
			  unsigned int data, int reg)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	if (devpriv->is_m_series) {
		m_series_stc_write(dev, data, reg);
	} else {
		spin_lock_irqsave(&devpriv->window_lock, flags);
		if (!devpriv->mite && reg < 8) {
			ni_writew(dev, data, reg * 2);
		} else {
			ni_writew(dev, reg, NI_E_STC_WINDOW_ADDR_REG);
			ni_writew(dev, data, NI_E_STC_WINDOW_DATA_REG);
		}
		spin_unlock_irqrestore(&devpriv->window_lock, flags);
	}
}

static void ni_stc_writel(struct comedi_device *dev,
			  unsigned int data, int reg)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv->is_m_series) {
		m_series_stc_write(dev, data, reg);
	} else {
		ni_stc_writew(dev, data >> 16, reg);
		ni_stc_writew(dev, data & 0xffff, reg + 1);
	}
}

static unsigned int ni_stc_readw(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	unsigned int val;

	if (devpriv->is_m_series) {
		val = m_series_stc_read(dev, reg);
	} else {
		spin_lock_irqsave(&devpriv->window_lock, flags);
		if (!devpriv->mite && reg < 8) {
			val = ni_readw(dev, reg * 2);
		} else {
			ni_writew(dev, reg, NI_E_STC_WINDOW_ADDR_REG);
			val = ni_readw(dev, NI_E_STC_WINDOW_DATA_REG);
		}
		spin_unlock_irqrestore(&devpriv->window_lock, flags);
	}
	return val;
}

static unsigned int ni_stc_readl(struct comedi_device *dev, int reg)
{
	struct ni_private *devpriv = dev->private;
	unsigned int val;

	if (devpriv->is_m_series) {
		val = m_series_stc_read(dev, reg);
	} else {
		val = ni_stc_readw(dev, reg) << 16;
		val |= ni_stc_readw(dev, reg + 1);
	}
	return val;
}

static inline void ni_set_bitfield(struct comedi_device *dev, int reg,
				   unsigned int bit_mask,
				   unsigned int bit_values)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->soft_reg_copy_lock, flags);
	switch (reg) {
	case NISTC_INTA_ENA_REG:
		devpriv->int_a_enable_reg &= ~bit_mask;
		devpriv->int_a_enable_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->int_a_enable_reg, reg);
		break;
	case NISTC_INTB_ENA_REG:
		devpriv->int_b_enable_reg &= ~bit_mask;
		devpriv->int_b_enable_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->int_b_enable_reg, reg);
		break;
	case NISTC_IO_BIDIR_PIN_REG:
		devpriv->io_bidirection_pin_reg &= ~bit_mask;
		devpriv->io_bidirection_pin_reg |= bit_values & bit_mask;
		ni_stc_writew(dev, devpriv->io_bidirection_pin_reg, reg);
		break;
	case NI_E_DMA_AI_AO_SEL_REG:
		devpriv->ai_ao_select_reg &= ~bit_mask;
		devpriv->ai_ao_select_reg |= bit_values & bit_mask;
		ni_writeb(dev, devpriv->ai_ao_select_reg, reg);
		break;
	case NI_E_DMA_G0_G1_SEL_REG:
		devpriv->g0_g1_select_reg &= ~bit_mask;
		devpriv->g0_g1_select_reg |= bit_values & bit_mask;
		ni_writeb(dev, devpriv->g0_g1_select_reg, reg);
		break;
	case NI_M_CDIO_DMA_SEL_REG:
		devpriv->cdio_dma_select_reg &= ~bit_mask;
		devpriv->cdio_dma_select_reg |= bit_values & bit_mask;
		ni_writeb(dev, devpriv->cdio_dma_select_reg, reg);
		break;
	default:
		dev_err(dev->class_dev, "called with invalid register %d\n",
			reg);
		break;
	}
	mmiowb();
	spin_unlock_irqrestore(&devpriv->soft_reg_copy_lock, flags);
}

#ifdef PCIDMA

/* selects the MITE channel to use for DMA */
#define NI_STC_DMA_CHAN_SEL(x)	(((x) < 4) ? BIT(x) :	\
				 ((x) == 4) ? 0x3 :	\
				 ((x) == 5) ? 0x5 : 0x0)

/* DMA channel setup */
static int ni_request_ai_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct mite_channel *mite_chan;
	unsigned long flags;
	unsigned int bits;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	mite_chan = mite_request_channel(devpriv->mite, devpriv->ai_mite_ring);
	if (!mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev,
			"failed to reserve mite dma channel for analog input\n");
		return -EBUSY;
	}
	mite_chan->dir = COMEDI_INPUT;
	devpriv->ai_mite_chan = mite_chan;

	bits = NI_STC_DMA_CHAN_SEL(mite_chan->channel);
	ni_set_bitfield(dev, NI_E_DMA_AI_AO_SEL_REG,
			NI_E_DMA_AI_SEL_MASK, NI_E_DMA_AI_SEL(bits));

	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static int ni_request_ao_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct mite_channel *mite_chan;
	unsigned long flags;
	unsigned int bits;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	mite_chan = mite_request_channel(devpriv->mite, devpriv->ao_mite_ring);
	if (!mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev,
			"failed to reserve mite dma channel for analog outut\n");
		return -EBUSY;
	}
	mite_chan->dir = COMEDI_OUTPUT;
	devpriv->ao_mite_chan = mite_chan;

	bits = NI_STC_DMA_CHAN_SEL(mite_chan->channel);
	ni_set_bitfield(dev, NI_E_DMA_AI_AO_SEL_REG,
			NI_E_DMA_AO_SEL_MASK, NI_E_DMA_AO_SEL(bits));

	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static int ni_request_gpct_mite_channel(struct comedi_device *dev,
					unsigned int gpct_index,
					enum comedi_io_direction direction)
{
	struct ni_private *devpriv = dev->private;
	struct ni_gpct *counter = &devpriv->counter_dev->counters[gpct_index];
	struct mite_channel *mite_chan;
	unsigned long flags;
	unsigned int bits;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	mite_chan = mite_request_channel(devpriv->mite,
					 devpriv->gpct_mite_ring[gpct_index]);
	if (!mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev,
			"failed to reserve mite dma channel for counter\n");
		return -EBUSY;
	}
	mite_chan->dir = direction;
	ni_tio_set_mite_channel(counter, mite_chan);

	bits = NI_STC_DMA_CHAN_SEL(mite_chan->channel);
	ni_set_bitfield(dev, NI_E_DMA_G0_G1_SEL_REG,
			NI_E_DMA_G0_G1_SEL_MASK(gpct_index),
			NI_E_DMA_G0_G1_SEL(gpct_index, bits));

	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}

static int ni_request_cdo_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct mite_channel *mite_chan;
	unsigned long flags;
	unsigned int bits;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	mite_chan = mite_request_channel(devpriv->mite, devpriv->cdo_mite_ring);
	if (!mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		dev_err(dev->class_dev,
			"failed to reserve mite dma channel for correlated digital output\n");
		return -EBUSY;
	}
	mite_chan->dir = COMEDI_OUTPUT;
	devpriv->cdo_mite_chan = mite_chan;

	/*
	 * XXX just guessing NI_STC_DMA_CHAN_SEL()
	 * returns the right bits, under the assumption the cdio dma
	 * selection works just like ai/ao/gpct.
	 * Definitely works for dma channels 0 and 1.
	 */
	bits = NI_STC_DMA_CHAN_SEL(mite_chan->channel);
	ni_set_bitfield(dev, NI_M_CDIO_DMA_SEL_REG,
			NI_M_CDIO_DMA_SEL_CDO_MASK,
			NI_M_CDIO_DMA_SEL_CDO(bits));

	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	return 0;
}
#endif /*  PCIDMA */

static void ni_release_ai_mite_channel(struct comedi_device *dev)
{
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->ai_mite_chan) {
		ni_set_bitfield(dev, NI_E_DMA_AI_AO_SEL_REG,
				NI_E_DMA_AI_SEL_MASK, 0);
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
		ni_set_bitfield(dev, NI_E_DMA_AI_AO_SEL_REG,
				NI_E_DMA_AO_SEL_MASK, 0);
		mite_release_channel(devpriv->ao_mite_chan);
		devpriv->ao_mite_chan = NULL;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
#endif /*  PCIDMA */
}

#ifdef PCIDMA
static void ni_release_gpct_mite_channel(struct comedi_device *dev,
					 unsigned int gpct_index)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->counter_dev->counters[gpct_index].mite_chan) {
		struct mite_channel *mite_chan =
		    devpriv->counter_dev->counters[gpct_index].mite_chan;

		ni_set_bitfield(dev, NI_E_DMA_G0_G1_SEL_REG,
				NI_E_DMA_G0_G1_SEL_MASK(gpct_index), 0);
		ni_tio_set_mite_channel(&devpriv->
					counter_dev->counters[gpct_index],
					NULL);
		mite_release_channel(mite_chan);
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static void ni_release_cdo_mite_channel(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan) {
		ni_set_bitfield(dev, NI_M_CDIO_DMA_SEL_REG,
				NI_M_CDIO_DMA_SEL_CDO_MASK, 0);
		mite_release_channel(devpriv->cdo_mite_chan);
		devpriv->cdo_mite_chan = NULL;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
}

static void ni_e_series_enable_second_irq(struct comedi_device *dev,
					  unsigned int gpct_index, short enable)
{
	struct ni_private *devpriv = dev->private;
	unsigned int val = 0;
	int reg;

	if (devpriv->is_m_series || gpct_index > 1)
		return;

	/*
	 * e-series boards use the second irq signals to generate
	 * dma requests for their counters
	 */
	if (gpct_index == 0) {
		reg = NISTC_INTA2_ENA_REG;
		if (enable)
			val = NISTC_INTA_ENA_G0_GATE;
	} else {
		reg = NISTC_INTB2_ENA_REG;
		if (enable)
			val = NISTC_INTB_ENA_G1_GATE;
	}
	ni_stc_writew(dev, val, reg);
}
#endif /*  PCIDMA */

static void ni_clear_ai_fifo(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	static const int timeout = 10000;
	int i;

	if (devpriv->is_6143) {
		/*  Flush the 6143 data FIFO */
		ni_writel(dev, 0x10, NI6143_AI_FIFO_CTRL_REG);
		ni_writel(dev, 0x00, NI6143_AI_FIFO_CTRL_REG);
		/*  Wait for complete */
		for (i = 0; i < timeout; i++) {
			if (!(ni_readl(dev, NI6143_AI_FIFO_STATUS_REG) & 0x10))
				break;
			udelay(1);
		}
		if (i == timeout)
			dev_err(dev->class_dev, "FIFO flush timeout\n");
	} else {
		ni_stc_writew(dev, 1, NISTC_ADC_FIFO_CLR_REG);
		if (devpriv->is_625x) {
			ni_writeb(dev, 0, NI_M_STATIC_AI_CTRL_REG(0));
			ni_writeb(dev, 1, NI_M_STATIC_AI_CTRL_REG(0));
#if 0
			/*
			 * The NI example code does 3 convert pulses for 625x
			 * boards, But that appears to be wrong in practice.
			 */
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
#endif
		}
	}
}

static inline void ni_ao_win_outw(struct comedi_device *dev,
				  unsigned int data, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, NI611X_AO_WINDOW_ADDR_REG);
	ni_writew(dev, data, NI611X_AO_WINDOW_DATA_REG);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static inline void ni_ao_win_outl(struct comedi_device *dev,
				  unsigned int data, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, NI611X_AO_WINDOW_ADDR_REG);
	ni_writel(dev, data, NI611X_AO_WINDOW_DATA_REG);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
}

static inline unsigned short ni_ao_win_inw(struct comedi_device *dev, int addr)
{
	struct ni_private *devpriv = dev->private;
	unsigned long flags;
	unsigned short data;

	spin_lock_irqsave(&devpriv->window_lock, flags);
	ni_writew(dev, addr, NI611X_AO_WINDOW_ADDR_REG);
	data = ni_readw(dev, NI611X_AO_WINDOW_DATA_REG);
	spin_unlock_irqrestore(&devpriv->window_lock, flags);
	return data;
}

/*
 * ni_set_bits( ) allows different parts of the ni_mio_common driver to
 * share registers (such as Interrupt_A_Register) without interfering with
 * each other.
 *
 * NOTE: the switch/case statements are optimized out for a constant argument
 * so this is actually quite fast---  If you must wrap another function around
 * this make it inline to avoid a large speed penalty.
 *
 * value should only be 1 or 0.
 */
static inline void ni_set_bits(struct comedi_device *dev, int reg,
			       unsigned int bits, unsigned int value)
{
	unsigned int bit_values;

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
		mite_sync_dma(devpriv->ai_mite_chan, s);
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
			if ((ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
			     NISTC_AI_STATUS1_FIFO_E) &&
			    mite_bytes_in_transit(devpriv->ai_mite_chan) == 0)
				break;
			udelay(5);
		}
		if (i == timeout) {
			dev_err(dev->class_dev, "timed out\n");
			dev_err(dev->class_dev,
				"mite_bytes_in_transit=%i, AI_Status1_Register=0x%x\n",
				mite_bytes_in_transit(devpriv->ai_mite_chan),
				ni_stc_readw(dev, NISTC_AI_STATUS1_REG));
			retval = -1;
		}
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	ni_sync_ai_dma(dev);

	return retval;
}

static int ni_ao_wait_for_dma_load(struct comedi_device *dev)
{
	static const int timeout = 10000;
	int i;

	for (i = 0; i < timeout; i++) {
		unsigned short b_status;

		b_status = ni_stc_readw(dev, NISTC_AO_STATUS1_REG);
		if (b_status & NISTC_AO_STATUS1_FIFO_HF)
			break;
		/*
		 * If we poll too often, the pci bus activity seems
		 * to slow the dma transfer down.
		 */
		usleep_range(10, 100);
	}
	if (i == timeout) {
		dev_err(dev->class_dev, "timed out waiting for dma load\n");
		return -EPIPE;
	}
	return 0;
}
#endif /* PCIDMA */

#ifndef PCIDMA

static void ni_ao_fifo_load(struct comedi_device *dev,
			    struct comedi_subdevice *s, int n)
{
	struct ni_private *devpriv = dev->private;
	int i;
	unsigned short d;
	unsigned int packed_data;

	for (i = 0; i < n; i++) {
		comedi_buf_read_samples(s, &d, 1);

		if (devpriv->is_6xxx) {
			packed_data = d & 0xffff;
			/* 6711 only has 16 bit wide ao fifo */
			if (!devpriv->is_6711) {
				comedi_buf_read_samples(s, &d, 1);
				i++;
				packed_data |= (d << 16) & 0xffff0000;
			}
			ni_writel(dev, packed_data, NI611X_AO_FIFO_DATA_REG);
		} else {
			ni_writew(dev, d, NI_E_AO_FIFO_DATA_REG);
		}
	}
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
	const struct ni_board_struct *board = dev->board_ptr;
	unsigned int nbytes;
	unsigned int nsamples;

	nbytes = comedi_buf_read_n_available(s);
	if (nbytes == 0) {
		s->async->events |= COMEDI_CB_OVERFLOW;
		return 0;
	}

	nsamples = comedi_bytes_to_samples(s, nbytes);
	if (nsamples > board->ao_fifo_depth / 2)
		nsamples = board->ao_fifo_depth / 2;

	ni_ao_fifo_load(dev, s, nsamples);

	return 1;
}

static int ni_ao_prep_fifo(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	unsigned int nbytes;
	unsigned int nsamples;

	/* reset fifo */
	ni_stc_writew(dev, 1, NISTC_DAC_FIFO_CLR_REG);
	if (devpriv->is_6xxx)
		ni_ao_win_outl(dev, 0x6, NI611X_AO_FIFO_OFFSET_LOAD_REG);

	/* load some data */
	nbytes = comedi_buf_read_n_available(s);
	if (nbytes == 0)
		return 0;

	nsamples = comedi_bytes_to_samples(s, nbytes);
	if (nsamples > board->ao_fifo_depth)
		nsamples = board->ao_fifo_depth;

	ni_ao_fifo_load(dev, s, nsamples);

	return nsamples;
}

static void ni_ai_fifo_read(struct comedi_device *dev,
			    struct comedi_subdevice *s, int n)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	unsigned int dl;
	unsigned short data;
	int i;

	if (devpriv->is_611x) {
		for (i = 0; i < n / 2; i++) {
			dl = ni_readl(dev, NI611X_AI_FIFO_DATA_REG);
			/* This may get the hi/lo data in the wrong order */
			data = (dl >> 16) & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
			data = dl & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}
		/* Check if there's a single sample stuck in the FIFO */
		if (n % 2) {
			dl = ni_readl(dev, NI611X_AI_FIFO_DATA_REG);
			data = dl & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}
	} else if (devpriv->is_6143) {
		/*
		 * This just reads the FIFO assuming the data is present,
		 * no checks on the FIFO status are performed.
		 */
		for (i = 0; i < n / 2; i++) {
			dl = ni_readl(dev, NI6143_AI_FIFO_DATA_REG);

			data = (dl >> 16) & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
			data = dl & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}
		if (n % 2) {
			/* Assume there is a single sample stuck in the FIFO */
			/* Get stranded sample into FIFO */
			ni_writel(dev, 0x01, NI6143_AI_FIFO_CTRL_REG);
			dl = ni_readl(dev, NI6143_AI_FIFO_DATA_REG);
			data = (dl >> 16) & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}
	} else {
		if (n > ARRAY_SIZE(devpriv->ai_fifo_buffer)) {
			dev_err(dev->class_dev,
				"bug! ai_fifo_buffer too small\n");
			async->events |= COMEDI_CB_ERROR;
			return;
		}
		for (i = 0; i < n; i++) {
			devpriv->ai_fifo_buffer[i] =
			    ni_readw(dev, NI_E_AI_FIFO_DATA_REG);
		}
		comedi_buf_write_samples(s, devpriv->ai_fifo_buffer, n);
	}
}

static void ni_handle_fifo_half_full(struct comedi_device *dev)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct comedi_subdevice *s = dev->read_subdev;
	int n;

	n = board->ai_fifo_depth / 2;

	ni_ai_fifo_read(dev, s, n);
}
#endif

/* Empties the AI fifo */
static void ni_handle_fifo_dregs(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned int dl;
	unsigned short data;
	int i;

	if (devpriv->is_611x) {
		while ((ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
			NISTC_AI_STATUS1_FIFO_E) == 0) {
			dl = ni_readl(dev, NI611X_AI_FIFO_DATA_REG);

			/* This may get the hi/lo data in the wrong order */
			data = dl >> 16;
			comedi_buf_write_samples(s, &data, 1);
			data = dl & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}
	} else if (devpriv->is_6143) {
		i = 0;
		while (ni_readl(dev, NI6143_AI_FIFO_STATUS_REG) & 0x04) {
			dl = ni_readl(dev, NI6143_AI_FIFO_DATA_REG);

			/* This may get the hi/lo data in the wrong order */
			data = dl >> 16;
			comedi_buf_write_samples(s, &data, 1);
			data = dl & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
			i += 2;
		}
		/*  Check if stranded sample is present */
		if (ni_readl(dev, NI6143_AI_FIFO_STATUS_REG) & 0x01) {
			/* Get stranded sample into FIFO */
			ni_writel(dev, 0x01, NI6143_AI_FIFO_CTRL_REG);
			dl = ni_readl(dev, NI6143_AI_FIFO_DATA_REG);
			data = (dl >> 16) & 0xffff;
			comedi_buf_write_samples(s, &data, 1);
		}

	} else {
		unsigned short fe;	/* fifo empty */

		fe = ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
		     NISTC_AI_STATUS1_FIFO_E;
		while (fe == 0) {
			for (i = 0;
			     i < ARRAY_SIZE(devpriv->ai_fifo_buffer); i++) {
				fe = ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
				     NISTC_AI_STATUS1_FIFO_E;
				if (fe)
					break;
				devpriv->ai_fifo_buffer[i] =
				    ni_readw(dev, NI_E_AI_FIFO_DATA_REG);
			}
			comedi_buf_write_samples(s, devpriv->ai_fifo_buffer, i);
		}
	}
}

static void get_last_sample_611x(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short data;
	unsigned int dl;

	if (!devpriv->is_611x)
		return;

	/* Check if there's a single sample stuck in the FIFO */
	if (ni_readb(dev, NI_E_STATUS_REG) & 0x80) {
		dl = ni_readl(dev, NI611X_AI_FIFO_DATA_REG);
		data = dl & 0xffff;
		comedi_buf_write_samples(s, &data, 1);
	}
}

static void get_last_sample_6143(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	unsigned short data;
	unsigned int dl;

	if (!devpriv->is_6143)
		return;

	/* Check if there's a single sample stuck in the FIFO */
	if (ni_readl(dev, NI6143_AI_FIFO_STATUS_REG) & 0x01) {
		/* Get stranded sample into FIFO */
		ni_writel(dev, 0x01, NI6143_AI_FIFO_CTRL_REG);
		dl = ni_readl(dev, NI6143_AI_FIFO_DATA_REG);

		/* This may get the hi/lo data in the wrong order */
		data = (dl >> 16) & 0xffff;
		comedi_buf_write_samples(s, &data, 1);
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
	/* handle special case of single scan */
	if (devpriv->ai_cmd2 & NISTC_AI_CMD2_END_ON_EOS)
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
	comedi_handle_events(dev, s);
#endif
}

static void ack_a_interrupt(struct comedi_device *dev, unsigned short a_status)
{
	unsigned short ack = 0;

	if (a_status & NISTC_AI_STATUS1_SC_TC)
		ack |= NISTC_INTA_ACK_AI_SC_TC;
	if (a_status & NISTC_AI_STATUS1_START1)
		ack |= NISTC_INTA_ACK_AI_START1;
	if (a_status & NISTC_AI_STATUS1_START)
		ack |= NISTC_INTA_ACK_AI_START;
	if (a_status & NISTC_AI_STATUS1_STOP)
		ack |= NISTC_INTA_ACK_AI_STOP;
	if (ack)
		ni_stc_writew(dev, ack, NISTC_INTA_ACK_REG);
}

static void handle_a_interrupt(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned short status)
{
	struct comedi_cmd *cmd = &s->async->cmd;

	/* test for all uncommon interrupt events at the same time */
	if (status & (NISTC_AI_STATUS1_ERR |
		      NISTC_AI_STATUS1_SC_TC | NISTC_AI_STATUS1_START1)) {
		if (status == 0xffff) {
			dev_err(dev->class_dev, "Card removed?\n");
			/*
			 * We probably aren't even running a command now,
			 * so it's a good idea to be careful.
			 */
			if (comedi_is_subdevice_running(s))
				s->async->events |= COMEDI_CB_ERROR;
			return;
		}
		if (status & NISTC_AI_STATUS1_ERR) {
			dev_err(dev->class_dev, "ai error a_status=%04x\n",
				status);

			shutdown_ai_command(dev);

			s->async->events |= COMEDI_CB_ERROR;
			if (status & NISTC_AI_STATUS1_OVER)
				s->async->events |= COMEDI_CB_OVERFLOW;
			return;
		}
		if (status & NISTC_AI_STATUS1_SC_TC) {
			if (cmd->stop_src == TRIG_COUNT)
				shutdown_ai_command(dev);
		}
	}
#ifndef PCIDMA
	if (status & NISTC_AI_STATUS1_FIFO_HF) {
		int i;
		static const int timeout = 10;
		/*
		 * PCMCIA cards (at least 6036) seem to stop producing
		 * interrupts if we fail to get the fifo less than half
		 * full, so loop to be sure.
		 */
		for (i = 0; i < timeout; ++i) {
			ni_handle_fifo_half_full(dev);
			if ((ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
			     NISTC_AI_STATUS1_FIFO_HF) == 0)
				break;
		}
	}
#endif /*  !PCIDMA */

	if (status & NISTC_AI_STATUS1_STOP)
		ni_handle_eos(dev, s);
}

static void ack_b_interrupt(struct comedi_device *dev, unsigned short b_status)
{
	unsigned short ack = 0;

	if (b_status & NISTC_AO_STATUS1_BC_TC)
		ack |= NISTC_INTB_ACK_AO_BC_TC;
	if (b_status & NISTC_AO_STATUS1_OVERRUN)
		ack |= NISTC_INTB_ACK_AO_ERR;
	if (b_status & NISTC_AO_STATUS1_START)
		ack |= NISTC_INTB_ACK_AO_START;
	if (b_status & NISTC_AO_STATUS1_START1)
		ack |= NISTC_INTB_ACK_AO_START1;
	if (b_status & NISTC_AO_STATUS1_UC_TC)
		ack |= NISTC_INTB_ACK_AO_UC_TC;
	if (b_status & NISTC_AO_STATUS1_UI2_TC)
		ack |= NISTC_INTB_ACK_AO_UI2_TC;
	if (b_status & NISTC_AO_STATUS1_UPDATE)
		ack |= NISTC_INTB_ACK_AO_UPDATE;
	if (ack)
		ni_stc_writew(dev, ack, NISTC_INTB_ACK_REG);
}

static void handle_b_interrupt(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned short b_status)
{
	if (b_status == 0xffff)
		return;
	if (b_status & NISTC_AO_STATUS1_OVERRUN) {
		dev_err(dev->class_dev,
			"AO FIFO underrun status=0x%04x status2=0x%04x\n",
			b_status, ni_stc_readw(dev, NISTC_AO_STATUS2_REG));
		s->async->events |= COMEDI_CB_OVERFLOW;
	}

	if (s->async->cmd.stop_src != TRIG_NONE &&
	    b_status & NISTC_AO_STATUS1_BC_TC)
		s->async->events |= COMEDI_CB_EOA;

#ifndef PCIDMA
	if (b_status & NISTC_AO_STATUS1_FIFO_REQ) {
		int ret;

		ret = ni_ao_fifo_half_empty(dev, s);
		if (!ret) {
			dev_err(dev->class_dev, "AO buffer underrun\n");
			ni_set_bits(dev, NISTC_INTB_ENA_REG,
				    NISTC_INTB_ENA_AO_FIFO |
				    NISTC_INTB_ENA_AO_ERR, 0);
			s->async->events |= COMEDI_CB_OVERFLOW;
		}
	}
#endif
}

static void ni_ai_munge(struct comedi_device *dev, struct comedi_subdevice *s,
			void *data, unsigned int num_bytes,
			unsigned int chan_index)
{
	struct ni_private *devpriv = dev->private;
	struct comedi_async *async = s->async;
	struct comedi_cmd *cmd = &async->cmd;
	unsigned int nsamples = comedi_bytes_to_samples(s, num_bytes);
	unsigned short *array = data;
	unsigned int *larray = data;
	unsigned int i;
#ifdef PCIDMA
	__le16 *barray = data;
	__le32 *blarray = data;
#endif

	for (i = 0; i < nsamples; i++) {
#ifdef PCIDMA
		if (s->subdev_flags & SDF_LSAMPL)
			larray[i] = le32_to_cpu(blarray[i]);
		else
			array[i] = le16_to_cpu(barray[i]);
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
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s = dev->read_subdev;
	int retval;
	unsigned long flags;

	retval = ni_request_ai_mite_channel(dev);
	if (retval)
		return retval;

	/* write alloc the entire buffer */
	comedi_buf_write_alloc(s, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (!devpriv->ai_mite_chan) {
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
		return -EIO;
	}

	if (devpriv->is_611x || devpriv->is_6143)
		mite_prep_dma(devpriv->ai_mite_chan, 32, 16);
	else if (devpriv->is_628x)
		mite_prep_dma(devpriv->ai_mite_chan, 32, 32);
	else
		mite_prep_dma(devpriv->ai_mite_chan, 16, 16);

	/*start the MITE */
	mite_dma_arm(devpriv->ai_mite_chan);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return 0;
}

static int ni_ao_setup_MITE_dma(struct comedi_device *dev)
{
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
		if (devpriv->is_611x || devpriv->is_6713) {
			mite_prep_dma(devpriv->ao_mite_chan, 32, 32);
		} else {
			/*
			 * Doing 32 instead of 16 bit wide transfers from
			 * memory makes the mite do 32 bit pci transfers,
			 * doubling pci bandwidth.
			 */
			mite_prep_dma(devpriv->ao_mite_chan, 16, 32);
		}
		mite_dma_arm(devpriv->ao_mite_chan);
	} else {
		retval = -EIO;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	return retval;
}

#endif /*  PCIDMA */

/*
 * used for both cancel ioctl and board initialization
 *
 * this is pretty harsh for a cancel, but it works...
 */
static int ni_ai_reset(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	unsigned int ai_personal;
	unsigned int ai_out_ctrl;

	ni_release_ai_mite_channel(dev);
	/* ai configuration */
	ni_stc_writew(dev, NISTC_RESET_AI_CFG_START | NISTC_RESET_AI,
		      NISTC_RESET_REG);

	ni_set_bits(dev, NISTC_INTA_ENA_REG, NISTC_INTA_ENA_AI_MASK, 0);

	ni_clear_ai_fifo(dev);

	if (!devpriv->is_6143)
		ni_writeb(dev, NI_E_MISC_CMD_EXT_ATRIG, NI_E_MISC_CMD_REG);

	ni_stc_writew(dev, NISTC_AI_CMD1_DISARM, NISTC_AI_CMD1_REG);
	ni_stc_writew(dev, NISTC_AI_MODE1_START_STOP |
			   NISTC_AI_MODE1_RSVD
			    /*| NISTC_AI_MODE1_TRIGGER_ONCE */,
		      NISTC_AI_MODE1_REG);
	ni_stc_writew(dev, 0, NISTC_AI_MODE2_REG);
	/* generate FIFO interrupts on non-empty */
	ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_NE,
		      NISTC_AI_MODE3_REG);

	ai_personal = NISTC_AI_PERSONAL_SHIFTIN_PW |
		      NISTC_AI_PERSONAL_SOC_POLARITY |
		      NISTC_AI_PERSONAL_LOCALMUX_CLK_PW;
	ai_out_ctrl = NISTC_AI_OUT_CTRL_SCAN_IN_PROG_SEL(3) |
		      NISTC_AI_OUT_CTRL_EXTMUX_CLK_SEL(0) |
		      NISTC_AI_OUT_CTRL_LOCALMUX_CLK_SEL(2) |
		      NISTC_AI_OUT_CTRL_SC_TC_SEL(3);
	if (devpriv->is_611x) {
		ai_out_ctrl |= NISTC_AI_OUT_CTRL_CONVERT_HIGH;
	} else if (devpriv->is_6143) {
		ai_out_ctrl |= NISTC_AI_OUT_CTRL_CONVERT_LOW;
	} else {
		ai_personal |= NISTC_AI_PERSONAL_CONVERT_PW;
		if (devpriv->is_622x)
			ai_out_ctrl |= NISTC_AI_OUT_CTRL_CONVERT_HIGH;
		else
			ai_out_ctrl |= NISTC_AI_OUT_CTRL_CONVERT_LOW;
	}
	ni_stc_writew(dev, ai_personal, NISTC_AI_PERSONAL_REG);
	ni_stc_writew(dev, ai_out_ctrl, NISTC_AI_OUT_CTRL_REG);

	/* the following registers should not be changed, because there
	 * are no backup registers in devpriv.  If you want to change
	 * any of these, add a backup register and other appropriate code:
	 *      NISTC_AI_MODE1_REG
	 *      NISTC_AI_MODE3_REG
	 *      NISTC_AI_PERSONAL_REG
	 *      NISTC_AI_OUT_CTRL_REG
	 */

	/* clear interrupts */
	ni_stc_writew(dev, NISTC_INTA_ACK_AI_ALL, NISTC_INTA_ACK_REG);

	ni_stc_writew(dev, NISTC_RESET_AI_CFG_END, NISTC_RESET_REG);

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

	ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE, NISTC_AI_CMD1_REG);
	for (i = 0; i < NI_TIMEOUT; ++i) {
		if (!(ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
		      NISTC_AI_STATUS1_FIFO_E)) {
			ni_stc_writew(dev, 1, NISTC_ADC_FIFO_CLR_REG);
			return;
		}
		udelay(1);
	}
	dev_err(dev->class_dev, "timeout loading channel/gain list\n");
}

static void ni_m_series_load_channelgain_list(struct comedi_device *dev,
					      unsigned int n_chan,
					      unsigned int *list)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	unsigned int chan, range, aref;
	unsigned int i;
	unsigned int dither;
	unsigned int range_code;

	ni_stc_writew(dev, 1, NISTC_CFG_MEM_CLR_REG);

	if ((list[0] & CR_ALT_SOURCE)) {
		unsigned int bypass_bits;

		chan = CR_CHAN(list[0]);
		range = CR_RANGE(list[0]);
		range_code = ni_gainlkup[board->gainlkup][range];
		dither = (list[0] & CR_ALT_FILTER) != 0;
		bypass_bits = NI_M_CFG_BYPASS_FIFO |
			      NI_M_CFG_BYPASS_AI_CHAN(chan) |
			      NI_M_CFG_BYPASS_AI_GAIN(range_code) |
			      devpriv->ai_calib_source;
		if (dither)
			bypass_bits |= NI_M_CFG_BYPASS_AI_DITHER;
		/*  don't use 2's complement encoding */
		bypass_bits |= NI_M_CFG_BYPASS_AI_POLARITY;
		ni_writel(dev, bypass_bits, NI_M_CFG_BYPASS_FIFO_REG);
	} else {
		ni_writel(dev, 0, NI_M_CFG_BYPASS_FIFO_REG);
	}
	for (i = 0; i < n_chan; i++) {
		unsigned int config_bits = 0;

		chan = CR_CHAN(list[i]);
		aref = CR_AREF(list[i]);
		range = CR_RANGE(list[i]);
		dither = (list[i] & CR_ALT_FILTER) != 0;

		range_code = ni_gainlkup[board->gainlkup][range];
		devpriv->ai_offset[i] = 0;
		switch (aref) {
		case AREF_DIFF:
			config_bits |= NI_M_AI_CFG_CHAN_TYPE_DIFF;
			break;
		case AREF_COMMON:
			config_bits |= NI_M_AI_CFG_CHAN_TYPE_COMMON;
			break;
		case AREF_GROUND:
			config_bits |= NI_M_AI_CFG_CHAN_TYPE_GROUND;
			break;
		case AREF_OTHER:
			break;
		}
		config_bits |= NI_M_AI_CFG_CHAN_SEL(chan);
		config_bits |= NI_M_AI_CFG_BANK_SEL(chan);
		config_bits |= NI_M_AI_CFG_GAIN(range_code);
		if (i == n_chan - 1)
			config_bits |= NI_M_AI_CFG_LAST_CHAN;
		if (dither)
			config_bits |= NI_M_AI_CFG_DITHER;
		/*  don't use 2's complement encoding */
		config_bits |= NI_M_AI_CFG_POLARITY;
		ni_writew(dev, config_bits, NI_M_AI_CFG_FIFO_DATA_REG);
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
				     struct comedi_subdevice *s,
				     unsigned int n_chan, unsigned int *list)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	unsigned int offset = (s->maxdata + 1) >> 1;
	unsigned int chan, range, aref;
	unsigned int i;
	unsigned int hi, lo;
	unsigned int dither;

	if (devpriv->is_m_series) {
		ni_m_series_load_channelgain_list(dev, n_chan, list);
		return;
	}
	if (n_chan == 1 && !devpriv->is_611x && !devpriv->is_6143) {
		if (devpriv->changain_state &&
		    devpriv->changain_spec == list[0]) {
			/*  ready to go. */
			return;
		}
		devpriv->changain_state = 1;
		devpriv->changain_spec = list[0];
	} else {
		devpriv->changain_state = 0;
	}

	ni_stc_writew(dev, 1, NISTC_CFG_MEM_CLR_REG);

	/*  Set up Calibration mode if required */
	if (devpriv->is_6143) {
		if ((list[0] & CR_ALT_SOURCE) &&
		    !devpriv->ai_calib_source_enabled) {
			/*  Strobe Relay enable bit */
			ni_writew(dev, devpriv->ai_calib_source |
				       NI6143_CALIB_CHAN_RELAY_ON,
				  NI6143_CALIB_CHAN_REG);
			ni_writew(dev, devpriv->ai_calib_source,
				  NI6143_CALIB_CHAN_REG);
			devpriv->ai_calib_source_enabled = 1;
			/* Allow relays to change */
			msleep_interruptible(100);
		} else if (!(list[0] & CR_ALT_SOURCE) &&
			   devpriv->ai_calib_source_enabled) {
			/*  Strobe Relay disable bit */
			ni_writew(dev, devpriv->ai_calib_source |
				       NI6143_CALIB_CHAN_RELAY_OFF,
				  NI6143_CALIB_CHAN_REG);
			ni_writew(dev, devpriv->ai_calib_source,
				  NI6143_CALIB_CHAN_REG);
			devpriv->ai_calib_source_enabled = 0;
			/* Allow relays to change */
			msleep_interruptible(100);
		}
	}

	for (i = 0; i < n_chan; i++) {
		if (!devpriv->is_6143 && (list[i] & CR_ALT_SOURCE))
			chan = devpriv->ai_calib_source;
		else
			chan = CR_CHAN(list[i]);
		aref = CR_AREF(list[i]);
		range = CR_RANGE(list[i]);
		dither = (list[i] & CR_ALT_FILTER) != 0;

		/* fix the external/internal range differences */
		range = ni_gainlkup[board->gainlkup][range];
		if (devpriv->is_611x)
			devpriv->ai_offset[i] = offset;
		else
			devpriv->ai_offset[i] = (range & 0x100) ? 0 : offset;

		hi = 0;
		if ((list[i] & CR_ALT_SOURCE)) {
			if (devpriv->is_611x)
				ni_writew(dev, CR_CHAN(list[i]) & 0x0003,
					  NI611X_CALIB_CHAN_SEL_REG);
		} else {
			if (devpriv->is_611x)
				aref = AREF_DIFF;
			else if (devpriv->is_6143)
				aref = AREF_OTHER;
			switch (aref) {
			case AREF_DIFF:
				hi |= NI_E_AI_CFG_HI_TYPE_DIFF;
				break;
			case AREF_COMMON:
				hi |= NI_E_AI_CFG_HI_TYPE_COMMON;
				break;
			case AREF_GROUND:
				hi |= NI_E_AI_CFG_HI_TYPE_GROUND;
				break;
			case AREF_OTHER:
				break;
			}
		}
		hi |= NI_E_AI_CFG_HI_CHAN(chan);

		ni_writew(dev, hi, NI_E_AI_CFG_HI_REG);

		if (!devpriv->is_6143) {
			lo = NI_E_AI_CFG_LO_GAIN(range);

			if (i == n_chan - 1)
				lo |= NI_E_AI_CFG_LO_LAST_CHAN;
			if (dither)
				lo |= NI_E_AI_CFG_LO_DITHER;

			ni_writew(dev, lo, NI_E_AI_CFG_LO_REG);
		}
	}

	/* prime the channel/gain list */
	if (!devpriv->is_611x && !devpriv->is_6143)
		ni_prime_channelgain_list(dev);
}

static int ni_ai_insn_read(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn,
			   unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int mask = (s->maxdata + 1) >> 1;
	int i, n;
	unsigned int signbits;
	unsigned int d;
	unsigned long dl;

	ni_load_channelgain_list(dev, s, 1, &insn->chanspec);

	ni_clear_ai_fifo(dev);

	signbits = devpriv->ai_offset[0];
	if (devpriv->is_611x) {
		for (n = 0; n < num_adc_stages_611x; n++) {
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
			udelay(1);
		}
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
			/* The 611x has screwy 32-bit FIFOs. */
			d = 0;
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (ni_readb(dev, NI_E_STATUS_REG) & 0x80) {
					d = ni_readl(dev,
						     NI611X_AI_FIFO_DATA_REG);
					d >>= 16;
					d &= 0xffff;
					break;
				}
				if (!(ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
				      NISTC_AI_STATUS1_FIFO_E)) {
					d = ni_readl(dev,
						     NI611X_AI_FIFO_DATA_REG);
					d &= 0xffff;
					break;
				}
			}
			if (i == NI_TIMEOUT) {
				dev_err(dev->class_dev, "timeout\n");
				return -ETIME;
			}
			d += signbits;
			data[n] = d;
		}
	} else if (devpriv->is_6143) {
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);

			/*
			 * The 6143 has 32-bit FIFOs. You need to strobe a
			 * bit to move a single 16bit stranded sample into
			 * the FIFO.
			 */
			dl = 0;
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (ni_readl(dev, NI6143_AI_FIFO_STATUS_REG) &
				    0x01) {
					/* Get stranded sample into FIFO */
					ni_writel(dev, 0x01,
						  NI6143_AI_FIFO_CTRL_REG);
					dl = ni_readl(dev,
						      NI6143_AI_FIFO_DATA_REG);
					break;
				}
			}
			if (i == NI_TIMEOUT) {
				dev_err(dev->class_dev, "timeout\n");
				return -ETIME;
			}
			data[n] = (((dl >> 16) & 0xFFFF) + signbits) & 0xFFFF;
		}
	} else {
		for (n = 0; n < insn->n; n++) {
			ni_stc_writew(dev, NISTC_AI_CMD1_CONVERT_PULSE,
				      NISTC_AI_CMD1_REG);
			for (i = 0; i < NI_TIMEOUT; i++) {
				if (!(ni_stc_readw(dev, NISTC_AI_STATUS1_REG) &
				      NISTC_AI_STATUS1_FIFO_E))
					break;
			}
			if (i == NI_TIMEOUT) {
				dev_err(dev->class_dev, "timeout\n");
				return -ETIME;
			}
			if (devpriv->is_m_series) {
				dl = ni_readl(dev, NI_M_AI_FIFO_DATA_REG);
				dl &= mask;
				data[n] = dl;
			} else {
				d = ni_readw(dev, NI_E_AI_FIFO_DATA_REG);
				/* subtle: needs to be short addition */
				d += signbits;
				data[n] = d;
			}
		}
	}
	return insn->n;
}

static int ni_ns_to_timer(const struct comedi_device *dev,
			  unsigned int nanosec, unsigned int flags)
{
	struct ni_private *devpriv = dev->private;
	int divider;

	switch (flags & CMDF_ROUND_MASK) {
	case CMDF_ROUND_NEAREST:
	default:
		divider = DIV_ROUND_CLOSEST(nanosec, devpriv->clock_ns);
		break;
	case CMDF_ROUND_DOWN:
		divider = (nanosec) / devpriv->clock_ns;
		break;
	case CMDF_ROUND_UP:
		divider = DIV_ROUND_UP(nanosec, devpriv->clock_ns);
		break;
	}
	return divider - 1;
}

static unsigned int ni_timer_to_ns(const struct comedi_device *dev, int timer)
{
	struct ni_private *devpriv = dev->private;

	return devpriv->clock_ns * (timer + 1);
}

static void ni_cmd_set_mite_transfer(struct mite_ring *ring,
				     struct comedi_subdevice *sdev,
				     const struct comedi_cmd *cmd,
				     unsigned int max_count) {
#ifdef PCIDMA
	unsigned int nbytes = max_count;

	if (cmd->stop_arg > 0 && cmd->stop_arg < max_count)
		nbytes = cmd->stop_arg;
	nbytes *= comedi_bytes_per_scan(sdev);

	if (nbytes > sdev->async->prealloc_bufsz) {
		if (cmd->stop_arg > 0)
			dev_err(sdev->device->class_dev,
				"ni_cmd_set_mite_transfer: tried exact data transfer limits greater than buffer size\n");

		/*
		 * we can only transfer up to the size of the buffer.  In this
		 * case, the user is expected to continue to write into the
		 * comedi buffer (already implemented as a ring buffer).
		 */
		nbytes = sdev->async->prealloc_bufsz;
	}

	mite_init_ring_descriptors(ring, sdev, nbytes);
#else
	dev_err(sdev->device->class_dev,
		"ni_cmd_set_mite_transfer: exact data transfer limits not implemented yet without DMA\n");
#endif
}

static unsigned int ni_min_ai_scan_period_ns(struct comedi_device *dev,
					     unsigned int num_channels)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;

	/* simultaneously-sampled inputs */
	if (devpriv->is_611x || devpriv->is_6143)
		return board->ai_speed;

	/* multiplexed inputs */
	return board->ai_speed * num_channels;
}

static int ni_ai_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp;
	unsigned int sources;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src,
					TRIG_NOW | TRIG_INT | TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);

	sources = TRIG_TIMER | TRIG_EXT;
	if (devpriv->is_611x || devpriv->is_6143)
		sources |= TRIG_NOW;
	err |= comedi_check_trigger_src(&cmd->convert_src, sources);

	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= comedi_check_trigger_is_unique(cmd->start_src);
	err |= comedi_check_trigger_is_unique(cmd->scan_begin_src);
	err |= comedi_check_trigger_is_unique(cmd->convert_src);
	err |= comedi_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	switch (cmd->start_src) {
	case TRIG_NOW:
	case TRIG_INT:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		tmp = CR_CHAN(cmd->start_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->start_arg & (CR_INVERT | CR_EDGE));
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, tmp);
		break;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
			ni_min_ai_scan_period_ns(dev, cmd->chanlist_len));
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg,
						    devpriv->clock_ns *
						    0xffffff);
	} else if (cmd->scan_begin_src == TRIG_EXT) {
		/* external trigger */
		unsigned int tmp = CR_CHAN(cmd->scan_begin_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->scan_begin_arg & (CR_INVERT | CR_EDGE));
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, tmp);
	} else {		/* TRIG_OTHER */
		err |= comedi_check_trigger_arg_is(&cmd->scan_begin_arg, 0);
	}

	if (cmd->convert_src == TRIG_TIMER) {
		if (devpriv->is_611x || devpriv->is_6143) {
			err |= comedi_check_trigger_arg_is(&cmd->convert_arg,
							   0);
		} else {
			err |= comedi_check_trigger_arg_min(&cmd->convert_arg,
							    board->ai_speed);
			err |= comedi_check_trigger_arg_max(&cmd->convert_arg,
							    devpriv->clock_ns *
							    0xffff);
		}
	} else if (cmd->convert_src == TRIG_EXT) {
		/* external trigger */
		unsigned int tmp = CR_CHAN(cmd->convert_arg);

		if (tmp > 16)
			tmp = 16;
		tmp |= (cmd->convert_arg & (CR_ALT_FILTER | CR_INVERT));
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, tmp);
	} else if (cmd->convert_src == TRIG_NOW) {
		err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	}

	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT) {
		unsigned int max_count = 0x01000000;

		if (devpriv->is_611x)
			max_count -= num_adc_stages_611x;
		err |= comedi_check_trigger_arg_max(&cmd->stop_arg, max_count);
		err |= comedi_check_trigger_arg_min(&cmd->stop_arg, 1);
	} else {
		/* TRIG_NONE */
		err |= comedi_check_trigger_arg_is(&cmd->stop_arg, 0);
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		cmd->scan_begin_arg =
		    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
						       cmd->scan_begin_arg,
						       cmd->flags));
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (!devpriv->is_611x && !devpriv->is_6143) {
			tmp = cmd->convert_arg;
			cmd->convert_arg =
			    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
							       cmd->convert_arg,
							       cmd->flags));
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

	ni_stc_writew(dev, NISTC_AI_CMD2_START1_PULSE | devpriv->ai_cmd2,
		      NISTC_AI_CMD2_REG);
	s->async->inttrig = NULL;

	return 1;
}

static int ni_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;
	int timer;
	int mode1 = 0;		/* mode1 is needed for both stop and convert */
	int mode2 = 0;
	int start_stop_select = 0;
	unsigned int stop_count;
	int interrupt_a_enable = 0;
	unsigned int ai_trig;

	if (dev->irq == 0) {
		dev_err(dev->class_dev, "cannot run command without an irq\n");
		return -EIO;
	}
	ni_clear_ai_fifo(dev);

	ni_load_channelgain_list(dev, s, cmd->chanlist_len, cmd->chanlist);

	/* start configuration */
	ni_stc_writew(dev, NISTC_RESET_AI_CFG_START, NISTC_RESET_REG);

	/*
	 * Disable analog triggering for now, since it interferes
	 * with the use of pfi0.
	 */
	devpriv->an_trig_etc_reg &= ~NISTC_ATRIG_ETC_ENA;
	ni_stc_writew(dev, devpriv->an_trig_etc_reg, NISTC_ATRIG_ETC_REG);

	ai_trig = NISTC_AI_TRIG_START2_SEL(0) | NISTC_AI_TRIG_START1_SYNC;
	switch (cmd->start_src) {
	case TRIG_INT:
	case TRIG_NOW:
		ai_trig |= NISTC_AI_TRIG_START1_EDGE |
			   NISTC_AI_TRIG_START1_SEL(0);
		break;
	case TRIG_EXT:
		ai_trig |= NISTC_AI_TRIG_START1_SEL(CR_CHAN(cmd->start_arg) +
						    1);

		if (cmd->start_arg & CR_INVERT)
			ai_trig |= NISTC_AI_TRIG_START1_POLARITY;
		if (cmd->start_arg & CR_EDGE)
			ai_trig |= NISTC_AI_TRIG_START1_EDGE;
		break;
	}
	ni_stc_writew(dev, ai_trig, NISTC_AI_TRIG_SEL_REG);

	mode2 &= ~NISTC_AI_MODE2_PRE_TRIGGER;
	mode2 &= ~NISTC_AI_MODE2_SC_INIT_LOAD_SRC;
	mode2 &= ~NISTC_AI_MODE2_SC_RELOAD_MODE;
	ni_stc_writew(dev, mode2, NISTC_AI_MODE2_REG);

	if (cmd->chanlist_len == 1 || devpriv->is_611x || devpriv->is_6143) {
		/* logic low */
		start_stop_select |= NISTC_AI_STOP_POLARITY |
				     NISTC_AI_STOP_SEL(31) |
				     NISTC_AI_STOP_SYNC;
	} else {
		/*  ai configuration memory */
		start_stop_select |= NISTC_AI_STOP_SEL(19);
	}
	ni_stc_writew(dev, start_stop_select, NISTC_AI_START_STOP_REG);

	devpriv->ai_cmd2 = 0;
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		stop_count = cmd->stop_arg - 1;

		if (devpriv->is_611x) {
			/*  have to take 3 stage adc pipeline into account */
			stop_count += num_adc_stages_611x;
		}
		/* stage number of scans */
		ni_stc_writel(dev, stop_count, NISTC_AI_SC_LOADA_REG);

		mode1 |= NISTC_AI_MODE1_START_STOP |
			 NISTC_AI_MODE1_RSVD |
			 NISTC_AI_MODE1_TRIGGER_ONCE;
		ni_stc_writew(dev, mode1, NISTC_AI_MODE1_REG);
		/* load SC (Scan Count) */
		ni_stc_writew(dev, NISTC_AI_CMD1_SC_LOAD, NISTC_AI_CMD1_REG);

		if (stop_count == 0) {
			devpriv->ai_cmd2 |= NISTC_AI_CMD2_END_ON_EOS;
			interrupt_a_enable |= NISTC_INTA_ENA_AI_STOP;
			/*
			 * This is required to get the last sample for
			 * chanlist_len > 1, not sure why.
			 */
			if (cmd->chanlist_len > 1)
				start_stop_select |= NISTC_AI_STOP_POLARITY |
						     NISTC_AI_STOP_EDGE;
		}
		break;
	case TRIG_NONE:
		/* stage number of scans */
		ni_stc_writel(dev, 0, NISTC_AI_SC_LOADA_REG);

		mode1 |= NISTC_AI_MODE1_START_STOP |
			 NISTC_AI_MODE1_RSVD |
			 NISTC_AI_MODE1_CONTINUOUS;
		ni_stc_writew(dev, mode1, NISTC_AI_MODE1_REG);

		/* load SC (Scan Count) */
		ni_stc_writew(dev, NISTC_AI_CMD1_SC_LOAD, NISTC_AI_CMD1_REG);
		break;
	}

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		/*
		 * stop bits for non 611x boards
		 * NISTC_AI_MODE3_SI_TRIG_DELAY=0
		 * NISTC_AI_MODE2_PRE_TRIGGER=0
		 * NISTC_AI_START_STOP_REG:
		 * NISTC_AI_START_POLARITY=0	(?) rising edge
		 * NISTC_AI_START_EDGE=1	edge triggered
		 * NISTC_AI_START_SYNC=1	(?)
		 * NISTC_AI_START_SEL=0		SI_TC
		 * NISTC_AI_STOP_POLARITY=0	rising edge
		 * NISTC_AI_STOP_EDGE=0		level
		 * NISTC_AI_STOP_SYNC=1
		 * NISTC_AI_STOP_SEL=19		external pin (configuration mem)
		 */
		start_stop_select |= NISTC_AI_START_EDGE | NISTC_AI_START_SYNC;
		ni_stc_writew(dev, start_stop_select, NISTC_AI_START_STOP_REG);

		mode2 &= ~NISTC_AI_MODE2_SI_INIT_LOAD_SRC;	/* A */
		mode2 |= NISTC_AI_MODE2_SI_RELOAD_MODE(0);
		/* mode2 |= NISTC_AI_MODE2_SC_RELOAD_MODE; */
		ni_stc_writew(dev, mode2, NISTC_AI_MODE2_REG);

		/* load SI */
		timer = ni_ns_to_timer(dev, cmd->scan_begin_arg,
				       CMDF_ROUND_NEAREST);
		ni_stc_writel(dev, timer, NISTC_AI_SI_LOADA_REG);
		ni_stc_writew(dev, NISTC_AI_CMD1_SI_LOAD, NISTC_AI_CMD1_REG);
		break;
	case TRIG_EXT:
		if (cmd->scan_begin_arg & CR_EDGE)
			start_stop_select |= NISTC_AI_START_EDGE;
		if (cmd->scan_begin_arg & CR_INVERT)	/* falling edge */
			start_stop_select |= NISTC_AI_START_POLARITY;
		if (cmd->scan_begin_src != cmd->convert_src ||
		    (cmd->scan_begin_arg & ~CR_EDGE) !=
		    (cmd->convert_arg & ~CR_EDGE))
			start_stop_select |= NISTC_AI_START_SYNC;
		start_stop_select |=
		    NISTC_AI_START_SEL(1 + CR_CHAN(cmd->scan_begin_arg));
		ni_stc_writew(dev, start_stop_select, NISTC_AI_START_STOP_REG);
		break;
	}

	switch (cmd->convert_src) {
	case TRIG_TIMER:
	case TRIG_NOW:
		if (cmd->convert_arg == 0 || cmd->convert_src == TRIG_NOW)
			timer = 1;
		else
			timer = ni_ns_to_timer(dev, cmd->convert_arg,
					       CMDF_ROUND_NEAREST);
		/* 0,0 does not work */
		ni_stc_writew(dev, 1, NISTC_AI_SI2_LOADA_REG);
		ni_stc_writew(dev, timer, NISTC_AI_SI2_LOADB_REG);

		mode2 &= ~NISTC_AI_MODE2_SI2_INIT_LOAD_SRC;	/* A */
		mode2 |= NISTC_AI_MODE2_SI2_RELOAD_MODE;	/* alternate */
		ni_stc_writew(dev, mode2, NISTC_AI_MODE2_REG);

		ni_stc_writew(dev, NISTC_AI_CMD1_SI2_LOAD, NISTC_AI_CMD1_REG);

		mode2 |= NISTC_AI_MODE2_SI2_INIT_LOAD_SRC;	/* B */
		mode2 |= NISTC_AI_MODE2_SI2_RELOAD_MODE;	/* alternate */
		ni_stc_writew(dev, mode2, NISTC_AI_MODE2_REG);
		break;
	case TRIG_EXT:
		mode1 |= NISTC_AI_MODE1_CONVERT_SRC(1 +
						    CR_CHAN(cmd->convert_arg));
		if ((cmd->convert_arg & CR_INVERT) == 0)
			mode1 |= NISTC_AI_MODE1_CONVERT_POLARITY;
		ni_stc_writew(dev, mode1, NISTC_AI_MODE1_REG);

		mode2 |= NISTC_AI_MODE2_SC_GATE_ENA |
			 NISTC_AI_MODE2_START_STOP_GATE_ENA;
		ni_stc_writew(dev, mode2, NISTC_AI_MODE2_REG);

		break;
	}

	if (dev->irq) {
		/* interrupt on FIFO, errors, SC_TC */
		interrupt_a_enable |= NISTC_INTA_ENA_AI_ERR |
				      NISTC_INTA_ENA_AI_SC_TC;

#ifndef PCIDMA
		interrupt_a_enable |= NISTC_INTA_ENA_AI_FIFO;
#endif

		if ((cmd->flags & CMDF_WAKE_EOS) ||
		    (devpriv->ai_cmd2 & NISTC_AI_CMD2_END_ON_EOS)) {
			/* wake on end-of-scan */
			devpriv->aimode = AIMODE_SCAN;
		} else {
			devpriv->aimode = AIMODE_HALF_FULL;
		}

		switch (devpriv->aimode) {
		case AIMODE_HALF_FULL:
			/* FIFO interrupts and DMA requests on half-full */
#ifdef PCIDMA
			ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_HF_E,
				      NISTC_AI_MODE3_REG);
#else
			ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_HF,
				      NISTC_AI_MODE3_REG);
#endif
			break;
		case AIMODE_SAMPLE:
			/*generate FIFO interrupts on non-empty */
			ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_NE,
				      NISTC_AI_MODE3_REG);
			break;
		case AIMODE_SCAN:
#ifdef PCIDMA
			ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_NE,
				      NISTC_AI_MODE3_REG);
#else
			ni_stc_writew(dev, NISTC_AI_MODE3_FIFO_MODE_HF,
				      NISTC_AI_MODE3_REG);
#endif
			interrupt_a_enable |= NISTC_INTA_ENA_AI_STOP;
			break;
		default:
			break;
		}

		/* clear interrupts */
		ni_stc_writew(dev, NISTC_INTA_ACK_AI_ALL, NISTC_INTA_ACK_REG);

		ni_set_bits(dev, NISTC_INTA_ENA_REG, interrupt_a_enable, 1);
	} else {
		/* interrupt on nothing */
		ni_set_bits(dev, NISTC_INTA_ENA_REG, ~0, 0);

		/* XXX start polling if necessary */
	}

	/* end configuration */
	ni_stc_writew(dev, NISTC_RESET_AI_CFG_END, NISTC_RESET_REG);

	switch (cmd->scan_begin_src) {
	case TRIG_TIMER:
		ni_stc_writew(dev, NISTC_AI_CMD1_SI2_ARM |
				   NISTC_AI_CMD1_SI_ARM |
				   NISTC_AI_CMD1_DIV_ARM |
				   NISTC_AI_CMD1_SC_ARM,
			      NISTC_AI_CMD1_REG);
		break;
	case TRIG_EXT:
		ni_stc_writew(dev, NISTC_AI_CMD1_SI2_ARM |
				   NISTC_AI_CMD1_SI_ARM |	/* XXX ? */
				   NISTC_AI_CMD1_DIV_ARM |
				   NISTC_AI_CMD1_SC_ARM,
			      NISTC_AI_CMD1_REG);
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
		ni_stc_writew(dev, NISTC_AI_CMD2_START1_PULSE |
				   devpriv->ai_cmd2,
			      NISTC_AI_CMD2_REG);
		s->async->inttrig = NULL;
	} else if (cmd->start_src == TRIG_EXT) {
		s->async->inttrig = NULL;
	} else {	/* TRIG_INT */
		s->async->inttrig = ni_ai_inttrig;
	}

	return 0;
}

static int ni_ai_insn_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	if (insn->n < 1)
		return -EINVAL;

	switch (data[0]) {
	case INSN_CONFIG_ALT_SOURCE:
		if (devpriv->is_m_series) {
			if (data[1] & ~NI_M_CFG_BYPASS_AI_CAL_MASK)
				return -EINVAL;
			devpriv->ai_calib_source = data[1];
		} else if (devpriv->is_6143) {
			unsigned int calib_source;

			calib_source = data[1] & 0xf;

			devpriv->ai_calib_source = calib_source;
			ni_writew(dev, calib_source, NI6143_CALIB_CHAN_REG);
		} else {
			unsigned int calib_source;
			unsigned int calib_source_adjust;

			calib_source = data[1] & 0xf;
			calib_source_adjust = (data[1] >> 4) & 0xff;

			if (calib_source >= 8)
				return -EINVAL;
			devpriv->ai_calib_source = calib_source;
			if (devpriv->is_611x) {
				ni_writeb(dev, calib_source_adjust,
					  NI611X_CAL_GAIN_SEL_REG);
			}
		}
		return 2;
	default:
		break;
	}

	return -EINVAL;
}

static void ni_ao_munge(struct comedi_device *dev, struct comedi_subdevice *s,
			void *data, unsigned int num_bytes,
			unsigned int chan_index)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int nsamples = comedi_bytes_to_samples(s, num_bytes);
	unsigned short *array = data;
	unsigned int i;
#ifdef PCIDMA
	__le16 buf, *barray = data;
#endif

	for (i = 0; i < nsamples; i++) {
		unsigned int range = CR_RANGE(cmd->chanlist[chan_index]);
		unsigned short val = array[i];

		/*
		 * Munge data from unsigned to two's complement for
		 * bipolar ranges.
		 */
		if (comedi_range_is_bipolar(s, range))
			val = comedi_offset_munge(s, val);
#ifdef PCIDMA
		buf = cpu_to_le16(val);
		barray[i] = buf;
#else
		array[i] = val;
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
	struct ni_private *devpriv = dev->private;
	unsigned int range;
	unsigned int chan;
	unsigned int conf;
	int i;
	int invert = 0;

	if (timed) {
		for (i = 0; i < s->n_chan; ++i) {
			devpriv->ao_conf[i] &= ~NI_M_AO_CFG_BANK_UPDATE_TIMED;
			ni_writeb(dev, devpriv->ao_conf[i],
				  NI_M_AO_CFG_BANK_REG(i));
			ni_writeb(dev, 0xf, NI_M_AO_WAVEFORM_ORDER_REG(i));
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
			conf |= NI_M_AO_CFG_BANK_REF_INT_10V;
			ni_writeb(dev, 0, NI_M_AO_REF_ATTENUATION_REG(chan));
			break;
		case 10000000:
			conf |= NI_M_AO_CFG_BANK_REF_INT_5V;
			ni_writeb(dev, 0, NI_M_AO_REF_ATTENUATION_REG(chan));
			break;
		case 4000000:
			conf |= NI_M_AO_CFG_BANK_REF_INT_10V;
			ni_writeb(dev, NI_M_AO_REF_ATTENUATION_X5,
				  NI_M_AO_REF_ATTENUATION_REG(chan));
			break;
		case 2000000:
			conf |= NI_M_AO_CFG_BANK_REF_INT_5V;
			ni_writeb(dev, NI_M_AO_REF_ATTENUATION_X5,
				  NI_M_AO_REF_ATTENUATION_REG(chan));
			break;
		default:
			dev_err(dev->class_dev,
				"bug! unhandled ao reference voltage\n");
			break;
		}
		switch (krange->max + krange->min) {
		case 0:
			conf |= NI_M_AO_CFG_BANK_OFFSET_0V;
			break;
		case 10000000:
			conf |= NI_M_AO_CFG_BANK_OFFSET_5V;
			break;
		default:
			dev_err(dev->class_dev,
				"bug! unhandled ao offset voltage\n");
			break;
		}
		if (timed)
			conf |= NI_M_AO_CFG_BANK_UPDATE_TIMED;
		ni_writeb(dev, conf, NI_M_AO_CFG_BANK_REG(chan));
		devpriv->ao_conf[chan] = conf;
		ni_writeb(dev, i, NI_M_AO_WAVEFORM_ORDER_REG(chan));
	}
	return invert;
}

static int ni_old_ao_config_chanlist(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     unsigned int chanspec[],
				     unsigned int n_chans)
{
	struct ni_private *devpriv = dev->private;
	unsigned int range;
	unsigned int chan;
	unsigned int conf;
	int i;
	int invert = 0;

	for (i = 0; i < n_chans; i++) {
		chan = CR_CHAN(chanspec[i]);
		range = CR_RANGE(chanspec[i]);
		conf = NI_E_AO_DACSEL(chan);

		if (comedi_range_is_bipolar(s, range)) {
			conf |= NI_E_AO_CFG_BIP;
			invert = (s->maxdata + 1) >> 1;
		} else {
			invert = 0;
		}
		if (comedi_range_is_external(s, range))
			conf |= NI_E_AO_EXT_REF;

		/* not all boards can deglitch, but this shouldn't hurt */
		if (chanspec[i] & CR_DEGLITCH)
			conf |= NI_E_AO_DEGLITCH;

		/* analog reference */
		/* AREF_OTHER connects AO ground to AI ground, i think */
		if (CR_AREF(chanspec[i]) == AREF_OTHER)
			conf |= NI_E_AO_GROUND_REF;

		ni_writew(dev, conf, NI_E_AO_CFG_REG);
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

	if (devpriv->is_6xxx) {
		ni_ao_win_outw(dev, 1 << chan, NI671X_AO_IMMEDIATE_REG);

		reg = NI671X_DAC_DIRECT_DATA_REG(chan);
	} else if (devpriv->is_m_series) {
		reg = NI_M_DAC_DIRECT_DATA_REG(chan);
	} else {
		reg = NI_E_DAC_DIRECT_DATA_REG(chan);
	}

	ni_ao_config_chanlist(dev, s, &insn->chanspec, 1, 0);

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];

		s->readback[chan] = val;

		if (devpriv->is_6xxx) {
			/*
			 * 6xxx boards have bipolar outputs, munge the
			 * unsigned comedi values to 2's complement
			 */
			val = comedi_offset_munge(s, val);

			ni_ao_win_outw(dev, val, reg);
		} else if (devpriv->is_m_series) {
			/*
			 * M-series boards use offset binary values for
			 * bipolar and uinpolar outputs
			 */
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

static int ni_ao_insn_config(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	unsigned int nbytes;

	switch (data[0]) {
	case INSN_CONFIG_GET_HARDWARE_BUFFER_SIZE:
		switch (data[1]) {
		case COMEDI_OUTPUT:
			nbytes = comedi_samples_to_bytes(s,
							 board->ao_fifo_depth);
			data[2] = 1 + nbytes;
			if (devpriv->mite)
				data[2] += devpriv->mite->fifo_size;
			break;
		case COMEDI_INPUT:
			data[2] = 0;
			break;
		default:
			return -EINVAL;
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
	struct ni_private *devpriv = dev->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int ret;
	int interrupt_b_bits;
	int i;
	static const int timeout = 1000;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	/*
	 * Null trig at beginning prevent ao start trigger from executing more
	 * than once per command (and doing things like trying to allocate the
	 * ao dma channel multiple times).
	 */
	s->async->inttrig = NULL;

	ni_set_bits(dev, NISTC_INTB_ENA_REG,
		    NISTC_INTB_ENA_AO_FIFO | NISTC_INTB_ENA_AO_ERR, 0);
	interrupt_b_bits = NISTC_INTB_ENA_AO_ERR;
#ifdef PCIDMA
	ni_stc_writew(dev, 1, NISTC_DAC_FIFO_CLR_REG);
	if (devpriv->is_6xxx)
		ni_ao_win_outl(dev, 0x6, NI611X_AO_FIFO_OFFSET_LOAD_REG);
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

	interrupt_b_bits |= NISTC_INTB_ENA_AO_FIFO;
#endif

	ni_stc_writew(dev, devpriv->ao_mode3 | NISTC_AO_MODE3_NOT_AN_UPDATE,
		      NISTC_AO_MODE3_REG);
	ni_stc_writew(dev, devpriv->ao_mode3, NISTC_AO_MODE3_REG);
	/* wait for DACs to be loaded */
	for (i = 0; i < timeout; i++) {
		udelay(1);
		if ((ni_stc_readw(dev, NISTC_STATUS2_REG) &
		     NISTC_STATUS2_AO_TMRDACWRS_IN_PROGRESS) == 0)
			break;
	}
	if (i == timeout) {
		dev_err(dev->class_dev,
			"timed out waiting for AO_TMRDACWRs_In_Progress_St to clear\n");
		return -EIO;
	}
	/*
	 * stc manual says we are need to clear error interrupt after
	 * AO_TMRDACWRs_In_Progress_St clears
	 */
	ni_stc_writew(dev, NISTC_INTB_ACK_AO_ERR, NISTC_INTB_ACK_REG);

	ni_set_bits(dev, NISTC_INTB_ENA_REG, interrupt_b_bits, 1);

	ni_stc_writew(dev, NISTC_AO_CMD1_UI_ARM |
			   NISTC_AO_CMD1_UC_ARM |
			   NISTC_AO_CMD1_BC_ARM |
			   devpriv->ao_cmd1,
		      NISTC_AO_CMD1_REG);

	ni_stc_writew(dev, NISTC_AO_CMD2_START1_PULSE | devpriv->ao_cmd2,
		      NISTC_AO_CMD2_REG);

	return 0;
}

/*
 * begin ni_ao_cmd.
 * Organized similar to NI-STC and MHDDK examples.
 * ni_ao_cmd is broken out into configuration sub-routines for clarity.
 */

static void ni_ao_cmd_personalize(struct comedi_device *dev,
				  const struct comedi_cmd *cmd)
{
	const struct ni_board_struct *board = dev->board_ptr;
	unsigned int bits;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	bits =
	  /* fast CPU interface--only eseries */
	  /* ((slow CPU interface) ? 0 : AO_Fast_CPU) | */
	  NISTC_AO_PERSONAL_BC_SRC_SEL  |
	  0 /* (use_original_pulse ? 0 : NISTC_AO_PERSONAL_UPDATE_TIMEBASE) */ |
	  /*
	   * FIXME:  start setting following bit when appropriate.  Need to
	   * determine whether board is E4 or E1.
	   * FROM MHHDK:
	   * if board is E4 or E1
	   *   Set bit "NISTC_AO_PERSONAL_UPDATE_PW" to 0
	   * else
	   *   set it to 1
	   */
	  NISTC_AO_PERSONAL_UPDATE_PW   |
	  /* FIXME:  when should we set following bit to zero? */
	  NISTC_AO_PERSONAL_TMRDACWR_PW |
	  (board->ao_fifo_depth ?
	    NISTC_AO_PERSONAL_FIFO_ENA : NISTC_AO_PERSONAL_DMA_PIO_CTRL)
	  ;
#if 0
	/*
	 * FIXME:
	 * add something like ".has_individual_dacs = 0" to ni_board_struct
	 * since, as F Hess pointed out, not all in m series have singles.  not
	 * sure if e-series all have duals...
	 */

	/*
	 * F Hess: windows driver does not set NISTC_AO_PERSONAL_NUM_DAC bit for
	 * 6281, verified with bus analyzer.
	 */
	if (devpriv->is_m_series)
		bits |= NISTC_AO_PERSONAL_NUM_DAC;
#endif
	ni_stc_writew(dev, bits, NISTC_AO_PERSONAL_REG);

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_trigger(struct comedi_device *dev,
				  const struct comedi_cmd *cmd)
{
	struct ni_private *devpriv = dev->private;
	unsigned int trigsel;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	/* sync */
	if (cmd->stop_src == TRIG_NONE) {
		devpriv->ao_mode1 |= NISTC_AO_MODE1_CONTINUOUS;
		devpriv->ao_mode1 &= ~NISTC_AO_MODE1_TRIGGER_ONCE;
	} else {
		devpriv->ao_mode1 &= ~NISTC_AO_MODE1_CONTINUOUS;
		devpriv->ao_mode1 |= NISTC_AO_MODE1_TRIGGER_ONCE;
	}
	ni_stc_writew(dev, devpriv->ao_mode1, NISTC_AO_MODE1_REG);

	if (cmd->start_src == TRIG_INT) {
		trigsel = NISTC_AO_TRIG_START1_EDGE |
			  NISTC_AO_TRIG_START1_SYNC;
	} else { /* TRIG_EXT */
		trigsel = NISTC_AO_TRIG_START1_SEL(CR_CHAN(cmd->start_arg) + 1);
		/* 0=active high, 1=active low. see daq-stc 3-24 (p186) */
		if (cmd->start_arg & CR_INVERT)
			trigsel |= NISTC_AO_TRIG_START1_POLARITY;
		/* 0=edge detection disabled, 1=enabled */
		if (cmd->start_arg & CR_EDGE)
			trigsel |= NISTC_AO_TRIG_START1_EDGE;
	}
	ni_stc_writew(dev, trigsel, NISTC_AO_TRIG_SEL_REG);

	/* AO_Delayed_START1 = 0, we do not support delayed start...yet */

	/* sync */
	/* select DA_START1 as PFI6/AO_START1 when configured as an output */
	devpriv->ao_mode3 &= ~NISTC_AO_MODE3_TRIG_LEN;
	ni_stc_writew(dev, devpriv->ao_mode3, NISTC_AO_MODE3_REG);

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_counters(struct comedi_device *dev,
				   const struct comedi_cmd *cmd)
{
	struct ni_private *devpriv = dev->private;
	/* Not supporting 'waveform staging' or 'local buffer with pauses' */

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);
	/*
	 * This relies on ao_mode1/(Trigger_Once | Continuous) being set in
	 * set_trigger above.  It is unclear whether we really need to re-write
	 * this register with these values.  The mhddk examples for e-series
	 * show writing this in both places, but the examples for m-series show
	 * a single write in the set_counters function (here).
	 */
	ni_stc_writew(dev, devpriv->ao_mode1, NISTC_AO_MODE1_REG);

	/* sync (upload number of buffer iterations -1) */
	/* indicate that we want to use BC_Load_A_Register as the source */
	devpriv->ao_mode2 &= ~NISTC_AO_MODE2_BC_INIT_LOAD_SRC;
	ni_stc_writew(dev, devpriv->ao_mode2, NISTC_AO_MODE2_REG);

	/*
	 * if the BC_TC interrupt is still issued in spite of UC, BC, UI
	 * ignoring BC_TC, then we will need to find a way to ignore that
	 * interrupt in continuous mode.
	 */
	ni_stc_writel(dev, 0, NISTC_AO_BC_LOADA_REG); /* iter once */

	/* sync (issue command to load number of buffer iterations -1) */
	ni_stc_writew(dev, NISTC_AO_CMD1_BC_LOAD, NISTC_AO_CMD1_REG);

	/* sync (upload number of updates in buffer) */
	/* indicate that we want to use UC_Load_A_Register as the source */
	devpriv->ao_mode2 &= ~NISTC_AO_MODE2_UC_INIT_LOAD_SRC;
	ni_stc_writew(dev, devpriv->ao_mode2, NISTC_AO_MODE2_REG);

	/*
	 * if a user specifies '0', this automatically assumes the entire 24bit
	 * address space is available for the (multiple iterations of single
	 * buffer) MISB.  Otherwise, stop_arg specifies the MISB length that
	 * will be used, regardless of whether we are in continuous mode or not.
	 * In continuous mode, the output will just iterate indefinitely over
	 * the MISB.
	 */
	{
		unsigned int stop_arg = cmd->stop_arg > 0 ?
			(cmd->stop_arg & 0xffffff) : 0xffffff;

		if (devpriv->is_m_series) {
			/*
			 * this is how the NI example code does it for m-series
			 * boards, verified correct with 6259
			 */
			ni_stc_writel(dev, stop_arg - 1, NISTC_AO_UC_LOADA_REG);

			/* sync (issue cmd to load number of updates in MISB) */
			ni_stc_writew(dev, NISTC_AO_CMD1_UC_LOAD,
				      NISTC_AO_CMD1_REG);
		} else {
			ni_stc_writel(dev, stop_arg, NISTC_AO_UC_LOADA_REG);

			/* sync (issue cmd to load number of updates in MISB) */
			ni_stc_writew(dev, NISTC_AO_CMD1_UC_LOAD,
				      NISTC_AO_CMD1_REG);

			/*
			 * sync (upload number of updates-1 in MISB)
			 * --eseries only?
			 */
			ni_stc_writel(dev, stop_arg - 1, NISTC_AO_UC_LOADA_REG);
		}
	}

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_update(struct comedi_device *dev,
				 const struct comedi_cmd *cmd)
{
	struct ni_private *devpriv = dev->private;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	/*
	 * zero out these bit fields to be set below. Does an ao-reset do this
	 * automatically?
	 */
	devpriv->ao_mode1 &= ~(
	  NISTC_AO_MODE1_UI_SRC_MASK         |
	  NISTC_AO_MODE1_UI_SRC_POLARITY     |
	  NISTC_AO_MODE1_UPDATE_SRC_MASK     |
	  NISTC_AO_MODE1_UPDATE_SRC_POLARITY
	);

	if (cmd->scan_begin_src == TRIG_TIMER) {
		unsigned int trigvar;

		devpriv->ao_cmd2  &= ~NISTC_AO_CMD2_BC_GATE_ENA;

		/*
		 * NOTE: there are several other ways of configuring internal
		 * updates, but we'll only support one for now:  using
		 * AO_IN_TIMEBASE, w/o waveform staging, w/o a delay between
		 * START1 and first update, and also w/o local buffer mode w/
		 * pauses.
		 */

		/*
		 * This is already done above:
		 * devpriv->ao_mode1 &= ~(
		 *   // set UPDATE_Source to UI_TC:
		 *   NISTC_AO_MODE1_UPDATE_SRC_MASK |
		 *   // set UPDATE_Source_Polarity to rising (required?)
		 *   NISTC_AO_MODE1_UPDATE_SRC_POLARITY |
		 *   // set UI_Source to AO_IN_TIMEBASE1:
		 *   NISTC_AO_MODE1_UI_SRC_MASK     |
		 *   // set UI_Source_Polarity to rising (required?)
		 *   NISTC_AO_MODE1_UI_SRC_POLARITY
		 * );
		 */

		/*
		 * TODO:  use ao_ui_clock_source to allow all possible signals
		 * to be routed to UI_Source_Select.  See tSTC.h for
		 * eseries/ni67xx and tMSeries.h for mseries.
		 */

		trigvar = ni_ns_to_timer(dev, cmd->scan_begin_arg,
					 CMDF_ROUND_NEAREST);

		/*
		 * Wait N TB3 ticks after the start trigger before
		 * clocking (N must be >=2).
		 */
		/* following line: 2-1 per STC */
		ni_stc_writel(dev, 1, NISTC_AO_UI_LOADA_REG);
		ni_stc_writew(dev, NISTC_AO_CMD1_UI_LOAD, NISTC_AO_CMD1_REG);
		/* following line: N-1 per STC */
		ni_stc_writel(dev, trigvar - 1, NISTC_AO_UI_LOADA_REG);
	} else { /* TRIG_EXT */
		/* FIXME:  assert scan_begin_arg != 0, ret failure otherwise */
		devpriv->ao_cmd2  |= NISTC_AO_CMD2_BC_GATE_ENA;
		devpriv->ao_mode1 |= NISTC_AO_MODE1_UPDATE_SRC(
					CR_CHAN(cmd->scan_begin_arg));
		if (cmd->scan_begin_arg & CR_INVERT)
			devpriv->ao_mode1 |= NISTC_AO_MODE1_UPDATE_SRC_POLARITY;
	}

	ni_stc_writew(dev, devpriv->ao_cmd2, NISTC_AO_CMD2_REG);
	ni_stc_writew(dev, devpriv->ao_mode1, NISTC_AO_MODE1_REG);
	devpriv->ao_mode2 &= ~(NISTC_AO_MODE2_UI_RELOAD_MODE(3) |
			       NISTC_AO_MODE2_UI_INIT_LOAD_SRC);
	ni_stc_writew(dev, devpriv->ao_mode2, NISTC_AO_MODE2_REG);

	/* Configure DAQ-STC for Timed update mode */
	devpriv->ao_cmd1 |= NISTC_AO_CMD1_DAC1_UPDATE_MODE |
			    NISTC_AO_CMD1_DAC0_UPDATE_MODE;
	/* We are not using UPDATE2-->don't have to set DACx_Source_Select */
	ni_stc_writew(dev, devpriv->ao_cmd1, NISTC_AO_CMD1_REG);

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_channels(struct comedi_device *dev,
				   struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int bits = 0;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	if (devpriv->is_6xxx) {
		unsigned int i;

		bits = 0;
		for (i = 0; i < cmd->chanlist_len; ++i) {
			int chan = CR_CHAN(cmd->chanlist[i]);

			bits |= 1 << chan;
			ni_ao_win_outw(dev, chan, NI611X_AO_WAVEFORM_GEN_REG);
		}
		ni_ao_win_outw(dev, bits, NI611X_AO_TIMED_REG);
	}

	ni_ao_config_chanlist(dev, s, cmd->chanlist, cmd->chanlist_len, 1);

	if (cmd->scan_end_arg > 1) {
		devpriv->ao_mode1 |= NISTC_AO_MODE1_MULTI_CHAN;
		bits = NISTC_AO_OUT_CTRL_CHANS(cmd->scan_end_arg - 1)
				 | NISTC_AO_OUT_CTRL_UPDATE_SEL_HIGHZ;

	} else {
		devpriv->ao_mode1 &= ~NISTC_AO_MODE1_MULTI_CHAN;
		bits = NISTC_AO_OUT_CTRL_UPDATE_SEL_HIGHZ;
		if (devpriv->is_m_series | devpriv->is_6xxx)
			bits |= NISTC_AO_OUT_CTRL_CHANS(0);
		else
			bits |= NISTC_AO_OUT_CTRL_CHANS(
					CR_CHAN(cmd->chanlist[0]));
	}

	ni_stc_writew(dev, devpriv->ao_mode1, NISTC_AO_MODE1_REG);
	ni_stc_writew(dev, bits,              NISTC_AO_OUT_CTRL_REG);

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_stop_conditions(struct comedi_device *dev,
					  const struct comedi_cmd *cmd)
{
	struct ni_private *devpriv = dev->private;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	devpriv->ao_mode3 |= NISTC_AO_MODE3_STOP_ON_OVERRUN_ERR;
	ni_stc_writew(dev, devpriv->ao_mode3, NISTC_AO_MODE3_REG);

	/*
	 * Since we are not supporting waveform staging, we ignore these errors:
	 * NISTC_AO_MODE3_STOP_ON_BC_TC_ERR,
	 * NISTC_AO_MODE3_STOP_ON_BC_TC_TRIG_ERR
	 */

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
}

static void ni_ao_cmd_set_fifo_mode(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	devpriv->ao_mode2 &= ~NISTC_AO_MODE2_FIFO_MODE_MASK;
#ifdef PCIDMA
	devpriv->ao_mode2 |= NISTC_AO_MODE2_FIFO_MODE_HF_F;
#else
	devpriv->ao_mode2 |= NISTC_AO_MODE2_FIFO_MODE_HF;
#endif
	/* NOTE:  this is where use_onboard_memory=True would be implemented */
	devpriv->ao_mode2 &= ~NISTC_AO_MODE2_FIFO_REXMIT_ENA;
	ni_stc_writew(dev, devpriv->ao_mode2, NISTC_AO_MODE2_REG);

	/* enable sending of ao fifo requests (dma request) */
	ni_stc_writew(dev, NISTC_AO_START_AOFREQ_ENA, NISTC_AO_START_SEL_REG);

	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);

	/* we are not supporting boards with virtual fifos */
}

static void ni_ao_cmd_set_interrupts(struct comedi_device *dev,
				     struct comedi_subdevice *s)
{
	if (s->async->cmd.stop_src == TRIG_COUNT)
		ni_set_bits(dev, NISTC_INTB_ENA_REG,
			    NISTC_INTB_ENA_AO_BC_TC, 1);

	s->async->inttrig = ni_ao_inttrig;
}

static int ni_ao_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;

	if (dev->irq == 0) {
		dev_err(dev->class_dev, "cannot run command without an irq");
		return -EIO;
	}

	/* ni_ao_reset should have already been done */
	ni_ao_cmd_personalize(dev, cmd);
	/* clearing fifo and preload happens elsewhere */

	ni_ao_cmd_set_trigger(dev, cmd);
	ni_ao_cmd_set_counters(dev, cmd);
	ni_ao_cmd_set_update(dev, cmd);
	ni_ao_cmd_set_channels(dev, s);
	ni_ao_cmd_set_stop_conditions(dev, cmd);
	ni_ao_cmd_set_fifo_mode(dev);
	ni_cmd_set_mite_transfer(devpriv->ao_mite_ring, s, cmd, 0x00ffffff);
	ni_ao_cmd_set_interrupts(dev, s);

	/*
	 * arm(ing) and star(ting) happen in ni_ao_inttrig, which _must_ be
	 * called for ao commands since 1) TRIG_NOW is not supported and 2) DMA
	 * must be setup and initially written to before arm/start happen.
	 */
	return 0;
}

/* end ni_ao_cmd */

static int ni_ao_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_cmd *cmd)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	int err = 0;
	unsigned int tmp;

	/* Step 1 : check if triggers are trivially valid */

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_INT | TRIG_EXT);
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

	switch (cmd->start_src) {
	case TRIG_INT:
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);
		break;
	case TRIG_EXT:
		tmp = CR_CHAN(cmd->start_arg);

		if (tmp > 18)
			tmp = 18;
		tmp |= (cmd->start_arg & (CR_INVERT | CR_EDGE));
		err |= comedi_check_trigger_arg_is(&cmd->start_arg, tmp);
		break;
	}

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= comedi_check_trigger_arg_min(&cmd->scan_begin_arg,
						    board->ao_speed);
		err |= comedi_check_trigger_arg_max(&cmd->scan_begin_arg,
						    devpriv->clock_ns *
						    0xffffff);
	}

	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);

	if (err)
		return 3;

	/* step 4: fix up any arguments */
	if (cmd->scan_begin_src == TRIG_TIMER) {
		tmp = cmd->scan_begin_arg;
		cmd->scan_begin_arg =
		    ni_timer_to_ns(dev, ni_ns_to_timer(dev,
						       cmd->scan_begin_arg,
						       cmd->flags));
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (err)
		return 4;

	return 0;
}

static int ni_ao_reset(struct comedi_device *dev, struct comedi_subdevice *s)
{
	/* See 3.6.1.2 "Resetting", of DAQ-STC Technical Reference Manual */

	/*
	 * In the following, the "--sync" comments are meant to denote
	 * asynchronous boundaries for setting the registers as described in the
	 * DAQ-STC mostly in the order also described in the DAQ-STC.
	 */

	struct ni_private *devpriv = dev->private;

	ni_release_ao_mite_channel(dev);

	/* --sync (reset AO) */
	if (devpriv->is_m_series)
		/* following example in mhddk for m-series */
		ni_stc_writew(dev, NISTC_RESET_AO, NISTC_RESET_REG);

	/*--sync (start config) */
	ni_stc_writew(dev, NISTC_RESET_AO_CFG_START, NISTC_RESET_REG);

	/*--sync (Disarm) */
	ni_stc_writew(dev, NISTC_AO_CMD1_DISARM, NISTC_AO_CMD1_REG);

	/*
	 * --sync
	 * (clear bunch of registers--mseries mhddk examples do not include
	 * this)
	 */
	devpriv->ao_cmd1  = 0;
	devpriv->ao_cmd2  = 0;
	devpriv->ao_mode1 = 0;
	devpriv->ao_mode2 = 0;
	if (devpriv->is_m_series)
		devpriv->ao_mode3 = NISTC_AO_MODE3_LAST_GATE_DISABLE;
	else
		devpriv->ao_mode3 = 0;

	ni_stc_writew(dev, 0, NISTC_AO_PERSONAL_REG);
	ni_stc_writew(dev, 0, NISTC_AO_CMD1_REG);
	ni_stc_writew(dev, 0, NISTC_AO_CMD2_REG);
	ni_stc_writew(dev, 0, NISTC_AO_MODE1_REG);
	ni_stc_writew(dev, 0, NISTC_AO_MODE2_REG);
	ni_stc_writew(dev, 0, NISTC_AO_OUT_CTRL_REG);
	ni_stc_writew(dev, devpriv->ao_mode3, NISTC_AO_MODE3_REG);
	ni_stc_writew(dev, 0, NISTC_AO_START_SEL_REG);
	ni_stc_writew(dev, 0, NISTC_AO_TRIG_SEL_REG);

	/*--sync (disable interrupts) */
	ni_set_bits(dev, NISTC_INTB_ENA_REG, ~0, 0);

	/*--sync (ack) */
	ni_stc_writew(dev, NISTC_AO_PERSONAL_BC_SRC_SEL, NISTC_AO_PERSONAL_REG);
	ni_stc_writew(dev, NISTC_INTB_ACK_AO_ALL, NISTC_INTB_ACK_REG);

	/*--not in DAQ-STC.  which doc? */
	if (devpriv->is_6xxx) {
		ni_ao_win_outw(dev, (1u << s->n_chan) - 1u,
			       NI671X_AO_IMMEDIATE_REG);
		ni_ao_win_outw(dev, NI611X_AO_MISC_CLEAR_WG,
			       NI611X_AO_MISC_REG);
	}
	ni_stc_writew(dev, NISTC_RESET_AO_CFG_END, NISTC_RESET_REG);
	/*--end */

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

	devpriv->dio_control &= ~NISTC_DIO_CTRL_DIR_MASK;
	devpriv->dio_control |= NISTC_DIO_CTRL_DIR(s->io_bits);
	ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);

	return insn->n;
}

static int ni_dio_insn_bits(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	struct ni_private *devpriv = dev->private;

	/* Make sure we're not using the serial part of the dio */
	if ((data[0] & (NISTC_DIO_SDIN | NISTC_DIO_SDOUT)) &&
	    devpriv->serial_interval_ns)
		return -EBUSY;

	if (comedi_dio_update_state(s, data)) {
		devpriv->dio_output &= ~NISTC_DIO_OUT_PARALLEL_MASK;
		devpriv->dio_output |= NISTC_DIO_OUT_PARALLEL(s->state);
		ni_stc_writew(dev, devpriv->dio_output, NISTC_DIO_OUT_REG);
	}

	data[1] = ni_stc_readw(dev, NISTC_DIO_IN_REG);

	return insn->n;
}

#ifdef PCIDMA
static int ni_m_series_dio_insn_config(struct comedi_device *dev,
				       struct comedi_subdevice *s,
				       struct comedi_insn *insn,
				       unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	ni_writel(dev, s->io_bits, NI_M_DIO_DIR_REG);

	return insn->n;
}

static int ni_m_series_dio_insn_bits(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		ni_writel(dev, s->state, NI_M_DIO_REG);

	data[1] = ni_readl(dev, NI_M_DIO_REG);

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

	err |= comedi_check_trigger_src(&cmd->start_src, TRIG_INT);
	err |= comedi_check_trigger_src(&cmd->scan_begin_src, TRIG_EXT);
	err |= comedi_check_trigger_src(&cmd->convert_src, TRIG_NOW);
	err |= comedi_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= comedi_check_trigger_src(&cmd->stop_src, TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */
	/* Step 2b : and mutually compatible */

	/* Step 3: check if arguments are trivially valid */

	err |= comedi_check_trigger_arg_is(&cmd->start_arg, 0);

	tmp = cmd->scan_begin_arg;
	tmp &= CR_PACK_FLAGS(NI_M_CDO_MODE_SAMPLE_SRC_MASK, 0, 0, CR_INVERT);
	if (tmp != cmd->scan_begin_arg)
		err |= -EINVAL;

	err |= comedi_check_trigger_arg_is(&cmd->convert_arg, 0);
	err |= comedi_check_trigger_arg_is(&cmd->scan_end_arg,
					   cmd->chanlist_len);
	err |= comedi_check_trigger_arg_max(&cmd->stop_arg,
					    s->async->prealloc_bufsz /
					    comedi_bytes_per_scan(s));

	if (err)
		return 3;

	/* Step 4: fix up any arguments */

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
	const unsigned int timeout = 1000;
	int retval = 0;
	unsigned int i;
	struct ni_private *devpriv = dev->private;
	unsigned long flags;

	if (trig_num != cmd->start_arg)
		return -EINVAL;

	s->async->inttrig = NULL;

	/* read alloc the entire buffer */
	comedi_buf_read_alloc(s, s->async->prealloc_bufsz);

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan) {
		mite_prep_dma(devpriv->cdo_mite_chan, 32, 32);
		mite_dma_arm(devpriv->cdo_mite_chan);
	} else {
		dev_err(dev->class_dev, "BUG: no cdo mite channel?\n");
		retval = -EIO;
	}
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);
	if (retval < 0)
		return retval;

	/*
	 * XXX not sure what interrupt C group does
	 * wait for dma to fill output fifo
	 * ni_writeb(dev, NI_M_INTC_ENA, NI_M_INTC_ENA_REG);
	 */
	for (i = 0; i < timeout; ++i) {
		if (ni_readl(dev, NI_M_CDIO_STATUS_REG) &
		    NI_M_CDIO_STATUS_CDO_FIFO_FULL)
			break;
		usleep_range(10, 100);
	}
	if (i == timeout) {
		dev_err(dev->class_dev, "dma failed to fill cdo fifo!\n");
		s->cancel(dev, s);
		return -EIO;
	}
	ni_writel(dev, NI_M_CDO_CMD_ARM |
		       NI_M_CDO_CMD_ERR_INT_ENA_SET |
		       NI_M_CDO_CMD_F_E_INT_ENA_SET,
		  NI_M_CDIO_CMD_REG);
	return retval;
}

static int ni_cdio_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	const struct comedi_cmd *cmd = &s->async->cmd;
	unsigned int cdo_mode_bits;
	int retval;

	ni_writel(dev, NI_M_CDO_CMD_RESET, NI_M_CDIO_CMD_REG);
	cdo_mode_bits = NI_M_CDO_MODE_FIFO_MODE |
			NI_M_CDO_MODE_HALT_ON_ERROR |
			NI_M_CDO_MODE_SAMPLE_SRC(CR_CHAN(cmd->scan_begin_arg));
	if (cmd->scan_begin_arg & CR_INVERT)
		cdo_mode_bits |= NI_M_CDO_MODE_POLARITY;
	ni_writel(dev, cdo_mode_bits, NI_M_CDO_MODE_REG);
	if (s->io_bits) {
		ni_writel(dev, s->state, NI_M_CDO_FIFO_DATA_REG);
		ni_writel(dev, NI_M_CDO_CMD_SW_UPDATE, NI_M_CDIO_CMD_REG);
		ni_writel(dev, s->io_bits, NI_M_CDO_MASK_ENA_REG);
	} else {
		dev_err(dev->class_dev,
			"attempted to run digital output command with no lines configured as outputs\n");
		return -EIO;
	}
	retval = ni_request_cdo_mite_channel(dev);
	if (retval < 0)
		return retval;

	ni_cmd_set_mite_transfer(devpriv->cdo_mite_ring, s, cmd,
				 s->async->prealloc_bufsz /
				 comedi_bytes_per_scan(s));

	s->async->inttrig = ni_cdo_inttrig;

	return 0;
}

static int ni_cdio_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	ni_writel(dev, NI_M_CDO_CMD_DISARM |
		       NI_M_CDO_CMD_ERR_INT_ENA_CLR |
		       NI_M_CDO_CMD_F_E_INT_ENA_CLR |
		       NI_M_CDO_CMD_F_REQ_INT_ENA_CLR,
		  NI_M_CDIO_CMD_REG);
	/*
	 * XXX not sure what interrupt C group does
	 * ni_writeb(dev, 0, NI_M_INTC_ENA_REG);
	 */
	ni_writel(dev, 0, NI_M_CDO_MASK_ENA_REG);
	ni_release_cdo_mite_channel(dev);
	return 0;
}

static void handle_cdio_interrupt(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	unsigned int cdio_status;
	struct comedi_subdevice *s = &dev->subdevices[NI_DIO_SUBDEV];
	unsigned long flags;

	spin_lock_irqsave(&devpriv->mite_channel_lock, flags);
	if (devpriv->cdo_mite_chan)
		mite_ack_linkc(devpriv->cdo_mite_chan, s, true);
	spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags);

	cdio_status = ni_readl(dev, NI_M_CDIO_STATUS_REG);
	if (cdio_status & NI_M_CDIO_STATUS_CDO_ERROR) {
		/* XXX just guessing this is needed and does something useful */
		ni_writel(dev, NI_M_CDO_CMD_ERR_INT_CONFIRM,
			  NI_M_CDIO_CMD_REG);
		s->async->events |= COMEDI_CB_OVERFLOW;
	}
	if (cdio_status & NI_M_CDIO_STATUS_CDO_FIFO_EMPTY) {
		ni_writel(dev, NI_M_CDO_CMD_F_E_INT_ENA_CLR,
			  NI_M_CDIO_CMD_REG);
		/* s->async->events |= COMEDI_CB_EOA; */
	}
	comedi_handle_events(dev, s);
}
#endif /*  PCIDMA */

static int ni_serial_hw_readwrite8(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   unsigned char data_out,
				   unsigned char *data_in)
{
	struct ni_private *devpriv = dev->private;
	unsigned int status1;
	int err = 0, count = 20;

	devpriv->dio_output &= ~NISTC_DIO_OUT_SERIAL_MASK;
	devpriv->dio_output |= NISTC_DIO_OUT_SERIAL(data_out);
	ni_stc_writew(dev, devpriv->dio_output, NISTC_DIO_OUT_REG);

	status1 = ni_stc_readw(dev, NISTC_STATUS1_REG);
	if (status1 & NISTC_STATUS1_SERIO_IN_PROG) {
		err = -EBUSY;
		goto error;
	}

	devpriv->dio_control |= NISTC_DIO_CTRL_HW_SER_START;
	ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);
	devpriv->dio_control &= ~NISTC_DIO_CTRL_HW_SER_START;

	/* Wait until STC says we're done, but don't loop infinitely. */
	while ((status1 = ni_stc_readw(dev, NISTC_STATUS1_REG)) &
	       NISTC_STATUS1_SERIO_IN_PROG) {
		/* Delay one bit per loop */
		udelay((devpriv->serial_interval_ns + 999) / 1000);
		if (--count < 0) {
			dev_err(dev->class_dev,
				"SPI serial I/O didn't finish in time!\n");
			err = -ETIME;
			goto error;
		}
	}

	/*
	 * Delay for last bit. This delay is absolutely necessary, because
	 * NISTC_STATUS1_SERIO_IN_PROG goes high one bit too early.
	 */
	udelay((devpriv->serial_interval_ns + 999) / 1000);

	if (data_in)
		*data_in = ni_stc_readw(dev, NISTC_DIO_SERIAL_IN_REG);

error:
	ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);

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
		/*
		 * Output current bit; note that we cannot touch s->state
		 * because it is a per-subdevice field, and serial is
		 * a separate subdevice from DIO.
		 */
		devpriv->dio_output &= ~NISTC_DIO_SDOUT;
		if (data_out & mask)
			devpriv->dio_output |= NISTC_DIO_SDOUT;
		ni_stc_writew(dev, devpriv->dio_output, NISTC_DIO_OUT_REG);

		/*
		 * Assert SDCLK (active low, inverted), wait for half of
		 * the delay, deassert SDCLK, and wait for the other half.
		 */
		devpriv->dio_control |= NISTC_DIO_SDCLK;
		ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);

		udelay((devpriv->serial_interval_ns + 999) / 2000);

		devpriv->dio_control &= ~NISTC_DIO_SDCLK;
		ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);

		udelay((devpriv->serial_interval_ns + 999) / 2000);

		/* Input current bit */
		if (ni_stc_readw(dev, NISTC_DIO_IN_REG) & NISTC_DIO_SDIN)
			input |= mask;
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
	unsigned int clk_fout = devpriv->clock_and_fout;
	int err = insn->n;
	unsigned char byte_out, byte_in = 0;

	if (insn->n != 2)
		return -EINVAL;

	switch (data[0]) {
	case INSN_CONFIG_SERIAL_CLOCK:
		devpriv->serial_hw_mode = 1;
		devpriv->dio_control |= NISTC_DIO_CTRL_HW_SER_ENA;

		if (data[1] == SERIAL_DISABLED) {
			devpriv->serial_hw_mode = 0;
			devpriv->dio_control &= ~(NISTC_DIO_CTRL_HW_SER_ENA |
						  NISTC_DIO_SDCLK);
			data[1] = SERIAL_DISABLED;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_600NS) {
			/*
			 * Warning: this clock speed is too fast to reliably
			 * control SCXI.
			 */
			devpriv->dio_control &= ~NISTC_DIO_CTRL_HW_SER_TIMEBASE;
			clk_fout |= NISTC_CLK_FOUT_SLOW_TIMEBASE;
			clk_fout &= ~NISTC_CLK_FOUT_DIO_SER_OUT_DIV2;
			data[1] = SERIAL_600NS;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_1_2US) {
			devpriv->dio_control &= ~NISTC_DIO_CTRL_HW_SER_TIMEBASE;
			clk_fout |= NISTC_CLK_FOUT_SLOW_TIMEBASE |
				    NISTC_CLK_FOUT_DIO_SER_OUT_DIV2;
			data[1] = SERIAL_1_2US;
			devpriv->serial_interval_ns = data[1];
		} else if (data[1] <= SERIAL_10US) {
			devpriv->dio_control |= NISTC_DIO_CTRL_HW_SER_TIMEBASE;
			clk_fout |= NISTC_CLK_FOUT_SLOW_TIMEBASE |
				    NISTC_CLK_FOUT_DIO_SER_OUT_DIV2;
			/*
			 * Note: NISTC_CLK_FOUT_DIO_SER_OUT_DIV2 only affects
			 * 600ns/1.2us. If you turn divide_by_2 off with the
			 * slow clock, you will still get 10us, except then
			 * all your delays are wrong.
			 */
			data[1] = SERIAL_10US;
			devpriv->serial_interval_ns = data[1];
		} else {
			devpriv->dio_control &= ~(NISTC_DIO_CTRL_HW_SER_ENA |
						  NISTC_DIO_SDCLK);
			devpriv->serial_hw_mode = 0;
			data[1] = (data[1] / 1000) * 1000;
			devpriv->serial_interval_ns = data[1];
		}
		devpriv->clock_and_fout = clk_fout;

		ni_stc_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);
		ni_stc_writew(dev, devpriv->clock_and_fout, NISTC_CLK_FOUT_REG);
		return 1;

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
			dev_err(dev->class_dev, "serial disabled!\n");
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
		ni_ao_win_outw(dev, NI_E_AO_DACSEL(i) | 0x0,
			       NI67XX_AO_CFG2_REG);
	}
	ni_ao_win_outw(dev, 0x0, NI67XX_AO_SP_UPDATES_REG);
}

static const struct mio_regmap ni_gpct_to_stc_regmap[] = {
	[NITIO_G0_AUTO_INC]	= { NISTC_G0_AUTOINC_REG, 2 },
	[NITIO_G1_AUTO_INC]	= { NISTC_G1_AUTOINC_REG, 2 },
	[NITIO_G0_CMD]		= { NISTC_G0_CMD_REG, 2 },
	[NITIO_G1_CMD]		= { NISTC_G1_CMD_REG, 2 },
	[NITIO_G0_HW_SAVE]	= { NISTC_G0_HW_SAVE_REG, 4 },
	[NITIO_G1_HW_SAVE]	= { NISTC_G1_HW_SAVE_REG, 4 },
	[NITIO_G0_SW_SAVE]	= { NISTC_G0_SAVE_REG, 4 },
	[NITIO_G1_SW_SAVE]	= { NISTC_G1_SAVE_REG, 4 },
	[NITIO_G0_MODE]		= { NISTC_G0_MODE_REG, 2 },
	[NITIO_G1_MODE]		= { NISTC_G1_MODE_REG, 2 },
	[NITIO_G0_LOADA]	= { NISTC_G0_LOADA_REG, 4 },
	[NITIO_G1_LOADA]	= { NISTC_G1_LOADA_REG, 4 },
	[NITIO_G0_LOADB]	= { NISTC_G0_LOADB_REG, 4 },
	[NITIO_G1_LOADB]	= { NISTC_G1_LOADB_REG, 4 },
	[NITIO_G0_INPUT_SEL]	= { NISTC_G0_INPUT_SEL_REG, 2 },
	[NITIO_G1_INPUT_SEL]	= { NISTC_G1_INPUT_SEL_REG, 2 },
	[NITIO_G0_CNT_MODE]	= { 0x1b0, 2 },	/* M-Series only */
	[NITIO_G1_CNT_MODE]	= { 0x1b2, 2 },	/* M-Series only */
	[NITIO_G0_GATE2]	= { 0x1b4, 2 },	/* M-Series only */
	[NITIO_G1_GATE2]	= { 0x1b6, 2 },	/* M-Series only */
	[NITIO_G01_STATUS]	= { NISTC_G01_STATUS_REG, 2 },
	[NITIO_G01_RESET]	= { NISTC_RESET_REG, 2 },
	[NITIO_G01_STATUS1]	= { NISTC_STATUS1_REG, 2 },
	[NITIO_G01_STATUS2]	= { NISTC_STATUS2_REG, 2 },
	[NITIO_G0_DMA_CFG]	= { 0x1b8, 2 },	/* M-Series only */
	[NITIO_G1_DMA_CFG]	= { 0x1ba, 2 },	/* M-Series only */
	[NITIO_G0_DMA_STATUS]	= { 0x1b8, 2 },	/* M-Series only */
	[NITIO_G1_DMA_STATUS]	= { 0x1ba, 2 },	/* M-Series only */
	[NITIO_G0_ABZ]		= { 0x1c0, 2 },	/* M-Series only */
	[NITIO_G1_ABZ]		= { 0x1c2, 2 },	/* M-Series only */
	[NITIO_G0_INT_ACK]	= { NISTC_INTA_ACK_REG, 2 },
	[NITIO_G1_INT_ACK]	= { NISTC_INTB_ACK_REG, 2 },
	[NITIO_G0_STATUS]	= { NISTC_AI_STATUS1_REG, 2 },
	[NITIO_G1_STATUS]	= { NISTC_AO_STATUS1_REG, 2 },
	[NITIO_G0_INT_ENA]	= { NISTC_INTA_ENA_REG, 2 },
	[NITIO_G1_INT_ENA]	= { NISTC_INTB_ENA_REG, 2 },
};

static unsigned int ni_gpct_to_stc_register(struct comedi_device *dev,
					    enum ni_gpct_register reg)
{
	const struct mio_regmap *regmap;

	if (reg < ARRAY_SIZE(ni_gpct_to_stc_regmap)) {
		regmap = &ni_gpct_to_stc_regmap[reg];
	} else {
		dev_warn(dev->class_dev, "%s: unhandled register=0x%x\n",
			 __func__, reg);
		return 0;
	}

	return regmap->mio_reg;
}

static void ni_gpct_write_register(struct ni_gpct *counter, unsigned int bits,
				   enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	unsigned int stc_register = ni_gpct_to_stc_register(dev, reg);

	if (stc_register == 0)
		return;

	switch (reg) {
		/* m-series only registers */
	case NITIO_G0_CNT_MODE:
	case NITIO_G1_CNT_MODE:
	case NITIO_G0_GATE2:
	case NITIO_G1_GATE2:
	case NITIO_G0_DMA_CFG:
	case NITIO_G1_DMA_CFG:
	case NITIO_G0_ABZ:
	case NITIO_G1_ABZ:
		ni_writew(dev, bits, stc_register);
		break;

		/* 32 bit registers */
	case NITIO_G0_LOADA:
	case NITIO_G1_LOADA:
	case NITIO_G0_LOADB:
	case NITIO_G1_LOADB:
		ni_stc_writel(dev, bits, stc_register);
		break;

		/* 16 bit registers */
	case NITIO_G0_INT_ENA:
		ni_set_bitfield(dev, stc_register,
				NISTC_INTA_ENA_G0_GATE | NISTC_INTA_ENA_G0_TC,
				bits);
		break;
	case NITIO_G1_INT_ENA:
		ni_set_bitfield(dev, stc_register,
				NISTC_INTB_ENA_G1_GATE | NISTC_INTB_ENA_G1_TC,
				bits);
		break;
	default:
		ni_stc_writew(dev, bits, stc_register);
	}
}

static unsigned int ni_gpct_read_register(struct ni_gpct *counter,
					  enum ni_gpct_register reg)
{
	struct comedi_device *dev = counter->counter_dev->dev;
	unsigned int stc_register = ni_gpct_to_stc_register(dev, reg);

	if (stc_register == 0)
		return 0;

	switch (reg) {
		/* m-series only registers */
	case NITIO_G0_DMA_STATUS:
	case NITIO_G1_DMA_STATUS:
		return ni_readw(dev, stc_register);

		/* 32 bit registers */
	case NITIO_G0_HW_SAVE:
	case NITIO_G1_HW_SAVE:
	case NITIO_G0_SW_SAVE:
	case NITIO_G1_SW_SAVE:
		return ni_stc_readl(dev, stc_register);

		/* 16 bit registers */
	default:
		return ni_stc_readw(dev, stc_register);
	}
}

static int ni_freq_out_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int val = NISTC_CLK_FOUT_TO_DIVIDER(devpriv->clock_and_fout);
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
		unsigned int val = data[insn->n - 1];

		devpriv->clock_and_fout &= ~NISTC_CLK_FOUT_ENA;
		ni_stc_writew(dev, devpriv->clock_and_fout, NISTC_CLK_FOUT_REG);
		devpriv->clock_and_fout &= ~NISTC_CLK_FOUT_DIVIDER_MASK;

		/* use the last data value to set the fout divider */
		devpriv->clock_and_fout |= NISTC_CLK_FOUT_DIVIDER(val);

		devpriv->clock_and_fout |= NISTC_CLK_FOUT_ENA;
		ni_stc_writew(dev, devpriv->clock_and_fout, NISTC_CLK_FOUT_REG);
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
			devpriv->clock_and_fout &= ~NISTC_CLK_FOUT_TIMEBASE_SEL;
			break;
		case NI_FREQ_OUT_TIMEBASE_2_CLOCK_SRC:
			devpriv->clock_and_fout |= NISTC_CLK_FOUT_TIMEBASE_SEL;
			break;
		default:
			return -EINVAL;
		}
		ni_stc_writew(dev, devpriv->clock_and_fout, NISTC_CLK_FOUT_REG);
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		if (devpriv->clock_and_fout & NISTC_CLK_FOUT_TIMEBASE_SEL) {
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

static int ni_8255_callback(struct comedi_device *dev,
			    int dir, int port, int data, unsigned long iobase)
{
	if (dir) {
		ni_writeb(dev, data, iobase + 2 * port);
		return 0;
	}

	return ni_readb(dev, iobase + 2 * port);
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
	unsigned int up_count, down_count;

	switch (data[0]) {
	case INSN_CONFIG_PWM_OUTPUT:
		switch (data[1]) {
		case CMDF_ROUND_NEAREST:
			up_count = DIV_ROUND_CLOSEST(data[2],
						     devpriv->clock_ns);
			break;
		case CMDF_ROUND_DOWN:
			up_count = data[2] / devpriv->clock_ns;
			break;
		case CMDF_ROUND_UP:
			up_count =
			    DIV_ROUND_UP(data[2], devpriv->clock_ns);
			break;
		default:
			return -EINVAL;
		}
		switch (data[3]) {
		case CMDF_ROUND_NEAREST:
			down_count = DIV_ROUND_CLOSEST(data[4],
						       devpriv->clock_ns);
			break;
		case CMDF_ROUND_DOWN:
			down_count = data[4] / devpriv->clock_ns;
			break;
		case CMDF_ROUND_UP:
			down_count =
			    DIV_ROUND_UP(data[4], devpriv->clock_ns);
			break;
		default:
			return -EINVAL;
		}
		if (up_count * devpriv->clock_ns != data[2] ||
		    down_count * devpriv->clock_ns != data[4]) {
			data[2] = up_count * devpriv->clock_ns;
			data[4] = down_count * devpriv->clock_ns;
			return -EAGAIN;
		}
		ni_writel(dev, NI_M_CAL_PWM_HIGH_TIME(up_count) |
			       NI_M_CAL_PWM_LOW_TIME(down_count),
			  NI_M_CAL_PWM_REG);
		devpriv->pwm_up_count = up_count;
		devpriv->pwm_down_count = down_count;
		return 5;
	case INSN_CONFIG_GET_PWM_OUTPUT:
		return ni_get_pwm_config(dev, data);
	default:
		return -EINVAL;
	}
	return 0;
}

static int ni_6143_pwm_config(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_insn *insn,
			      unsigned int *data)
{
	struct ni_private *devpriv = dev->private;
	unsigned int up_count, down_count;

	switch (data[0]) {
	case INSN_CONFIG_PWM_OUTPUT:
		switch (data[1]) {
		case CMDF_ROUND_NEAREST:
			up_count = DIV_ROUND_CLOSEST(data[2],
						     devpriv->clock_ns);
			break;
		case CMDF_ROUND_DOWN:
			up_count = data[2] / devpriv->clock_ns;
			break;
		case CMDF_ROUND_UP:
			up_count =
			    DIV_ROUND_UP(data[2], devpriv->clock_ns);
			break;
		default:
			return -EINVAL;
		}
		switch (data[3]) {
		case CMDF_ROUND_NEAREST:
			down_count = DIV_ROUND_CLOSEST(data[4],
						       devpriv->clock_ns);
			break;
		case CMDF_ROUND_DOWN:
			down_count = data[4] / devpriv->clock_ns;
			break;
		case CMDF_ROUND_UP:
			down_count =
			    DIV_ROUND_UP(data[4], devpriv->clock_ns);
			break;
		default:
			return -EINVAL;
		}
		if (up_count * devpriv->clock_ns != data[2] ||
		    down_count * devpriv->clock_ns != data[4]) {
			data[2] = up_count * devpriv->clock_ns;
			data[4] = down_count * devpriv->clock_ns;
			return -EAGAIN;
		}
		ni_writel(dev, up_count, NI6143_CALIB_HI_TIME_REG);
		devpriv->pwm_up_count = up_count;
		ni_writel(dev, down_count, NI6143_CALIB_LO_TIME_REG);
		devpriv->pwm_down_count = down_count;
		return 5;
	case INSN_CONFIG_GET_PWM_OUTPUT:
		return ni_get_pwm_config(dev, data);
	default:
		return -EINVAL;
	}
	return 0;
}

static int pack_mb88341(int addr, int val, int *bitstring)
{
	/*
	 * Fujitsu MB 88341
	 * Note that address bits are reversed.  Thanks to
	 * Ingo Keen for noticing this.
	 *
	 * Note also that the 88341 expects address values from
	 * 1-12, whereas we use channel numbers 0-11.  The NI
	 * docs use 1-12, also, so be careful here.
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
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	unsigned int loadbit = 0, bits = 0, bit, bitstring = 0;
	unsigned int cmd;
	int i;
	int type;

	if (devpriv->caldacs[addr] == val)
		return;
	devpriv->caldacs[addr] = val;

	for (i = 0; i < 3; i++) {
		type = board->caldac[i];
		if (type == caldac_none)
			break;
		if (addr < caldacs[type].n_chans) {
			bits = caldacs[type].packbits(addr, val, &bitstring);
			loadbit = NI_E_SERIAL_CMD_DAC_LD(i);
			break;
		}
		addr -= caldacs[type].n_chans;
	}

	/* bits will be 0 if there is no caldac for the given addr */
	if (bits == 0)
		return;

	for (bit = 1 << (bits - 1); bit; bit >>= 1) {
		cmd = (bit & bitstring) ? NI_E_SERIAL_CMD_SDATA : 0;
		ni_writeb(dev, cmd, NI_E_SERIAL_CMD_REG);
		udelay(1);
		ni_writeb(dev, NI_E_SERIAL_CMD_SCLK | cmd, NI_E_SERIAL_CMD_REG);
		udelay(1);
	}
	ni_writeb(dev, loadbit, NI_E_SERIAL_CMD_REG);
	udelay(1);
	ni_writeb(dev, 0, NI_E_SERIAL_CMD_REG);
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
	const struct ni_board_struct *board = dev->board_ptr;
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
		unsigned int *maxdata_list = devpriv->caldac_maxdata_list;

		if (n_chans > MAX_N_CALDACS)
			dev_err(dev->class_dev,
				"BUG! MAX_N_CALDACS too small\n");
		s->maxdata_list = maxdata_list;
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
	unsigned int cmd = NI_E_SERIAL_CMD_EEPROM_CS;
	int bit;
	int bitstring;

	bitstring = 0x0300 | ((addr & 0x100) << 3) | (addr & 0xff);
	ni_writeb(dev, cmd, NI_E_SERIAL_CMD_REG);
	for (bit = 0x8000; bit; bit >>= 1) {
		if (bit & bitstring)
			cmd |= NI_E_SERIAL_CMD_SDATA;
		else
			cmd &= ~NI_E_SERIAL_CMD_SDATA;

		ni_writeb(dev, cmd, NI_E_SERIAL_CMD_REG);
		ni_writeb(dev, NI_E_SERIAL_CMD_SCLK | cmd, NI_E_SERIAL_CMD_REG);
	}
	cmd = NI_E_SERIAL_CMD_EEPROM_CS;
	bitstring = 0;
	for (bit = 0x80; bit; bit >>= 1) {
		ni_writeb(dev, cmd, NI_E_SERIAL_CMD_REG);
		ni_writeb(dev, NI_E_SERIAL_CMD_SCLK | cmd, NI_E_SERIAL_CMD_REG);
		if (ni_readb(dev, NI_E_STATUS_REG) & NI_E_STATUS_PROMOUT)
			bitstring |= bit;
	}
	ni_writeb(dev, 0, NI_E_SERIAL_CMD_REG);

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

static unsigned int ni_old_get_pfi_routing(struct comedi_device *dev,
					   unsigned int chan)
{
	/*  pre-m-series boards have fixed signals on pfi pins */
	switch (chan) {
	case 0:
		return NI_PFI_OUTPUT_AI_START1;
	case 1:
		return NI_PFI_OUTPUT_AI_START2;
	case 2:
		return NI_PFI_OUTPUT_AI_CONVERT;
	case 3:
		return NI_PFI_OUTPUT_G_SRC1;
	case 4:
		return NI_PFI_OUTPUT_G_GATE1;
	case 5:
		return NI_PFI_OUTPUT_AO_UPDATE_N;
	case 6:
		return NI_PFI_OUTPUT_AO_START1;
	case 7:
		return NI_PFI_OUTPUT_AI_START_PULSE;
	case 8:
		return NI_PFI_OUTPUT_G_SRC0;
	case 9:
		return NI_PFI_OUTPUT_G_GATE0;
	default:
		dev_err(dev->class_dev, "bug, unhandled case in switch.\n");
		break;
	}
	return 0;
}

static int ni_old_set_pfi_routing(struct comedi_device *dev,
				  unsigned int chan, unsigned int source)
{
	/*  pre-m-series boards have fixed signals on pfi pins */
	if (source != ni_old_get_pfi_routing(dev, chan))
		return -EINVAL;
	return 2;
}

static unsigned int ni_m_series_get_pfi_routing(struct comedi_device *dev,
						unsigned int chan)
{
	struct ni_private *devpriv = dev->private;
	const unsigned int array_offset = chan / 3;

	return NI_M_PFI_OUT_SEL_TO_SRC(chan,
				devpriv->pfi_output_select_reg[array_offset]);
}

static int ni_m_series_set_pfi_routing(struct comedi_device *dev,
				       unsigned int chan, unsigned int source)
{
	struct ni_private *devpriv = dev->private;
	unsigned int index = chan / 3;
	unsigned short val = devpriv->pfi_output_select_reg[index];

	if ((source & 0x1f) != source)
		return -EINVAL;

	val &= ~NI_M_PFI_OUT_SEL_MASK(chan);
	val |= NI_M_PFI_OUT_SEL(chan, source);
	ni_writew(dev, val, NI_M_PFI_OUT_SEL_REG(index));
	devpriv->pfi_output_select_reg[index] = val;

	return 2;
}

static unsigned int ni_get_pfi_routing(struct comedi_device *dev,
				       unsigned int chan)
{
	struct ni_private *devpriv = dev->private;

	return (devpriv->is_m_series)
			? ni_m_series_get_pfi_routing(dev, chan)
			: ni_old_get_pfi_routing(dev, chan);
}

static int ni_set_pfi_routing(struct comedi_device *dev,
			      unsigned int chan, unsigned int source)
{
	struct ni_private *devpriv = dev->private;

	return (devpriv->is_m_series)
			? ni_m_series_set_pfi_routing(dev, chan, source)
			: ni_old_set_pfi_routing(dev, chan, source);
}

static int ni_config_filter(struct comedi_device *dev,
			    unsigned int pfi_channel,
			    enum ni_pfi_filter_select filter)
{
	struct ni_private *devpriv = dev->private;
	unsigned int bits;

	if (!devpriv->is_m_series)
		return -ENOTSUPP;

	bits = ni_readl(dev, NI_M_PFI_FILTER_REG);
	bits &= ~NI_M_PFI_FILTER_SEL_MASK(pfi_channel);
	bits |= NI_M_PFI_FILTER_SEL(pfi_channel, filter);
	ni_writel(dev, bits, NI_M_PFI_FILTER_REG);
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
		ni_set_bits(dev, NISTC_IO_BIDIR_PIN_REG, 1 << chan, 1);
		break;
	case COMEDI_INPUT:
		ni_set_bits(dev, NISTC_IO_BIDIR_PIN_REG, 1 << chan, 0);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (devpriv->io_bidirection_pin_reg & (1 << chan)) ?
		    COMEDI_OUTPUT : COMEDI_INPUT;
		return 0;
	case INSN_CONFIG_SET_ROUTING:
		return ni_set_pfi_routing(dev, chan, data[1]);
	case INSN_CONFIG_GET_ROUTING:
		data[1] = ni_get_pfi_routing(dev, chan);
		break;
	case INSN_CONFIG_FILTER:
		return ni_config_filter(dev, chan, data[1]);
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
		ni_writew(dev, s->state, NI_M_PFI_DO_REG);

	data[1] = ni_readw(dev, NI_M_PFI_DI_REG);

	return insn->n;
}

static int cs5529_wait_for_idle(struct comedi_device *dev)
{
	unsigned short status;
	const int timeout = HZ;
	int i;

	for (i = 0; i < timeout; i++) {
		status = ni_ao_win_inw(dev, NI67XX_CAL_STATUS_REG);
		if ((status & NI67XX_CAL_STATUS_BUSY) == 0)
			break;
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(1))
			return -EIO;
	}
	if (i == timeout) {
		dev_err(dev->class_dev, "timeout\n");
		return -ETIME;
	}
	return 0;
}

static void cs5529_command(struct comedi_device *dev, unsigned short value)
{
	static const int timeout = 100;
	int i;

	ni_ao_win_outw(dev, value, NI67XX_CAL_CMD_REG);
	/* give time for command to start being serially clocked into cs5529.
	 * this insures that the NI67XX_CAL_STATUS_BUSY bit will get properly
	 * set before we exit this function.
	 */
	for (i = 0; i < timeout; i++) {
		if (ni_ao_win_inw(dev, NI67XX_CAL_STATUS_REG) &
		    NI67XX_CAL_STATUS_BUSY)
			break;
		udelay(1);
	}
	if (i == timeout)
		dev_err(dev->class_dev,
			"possible problem - never saw adc go busy?\n");
}

static int cs5529_do_conversion(struct comedi_device *dev,
				unsigned short *data)
{
	int retval;
	unsigned short status;

	cs5529_command(dev, CS5529_CMD_CB | CS5529_CMD_SINGLE_CONV);
	retval = cs5529_wait_for_idle(dev);
	if (retval) {
		dev_err(dev->class_dev,
			"timeout or signal in cs5529_do_conversion()\n");
		return -ETIME;
	}
	status = ni_ao_win_inw(dev, NI67XX_CAL_STATUS_REG);
	if (status & NI67XX_CAL_STATUS_OSC_DETECT) {
		dev_err(dev->class_dev,
			"cs5529 conversion error, status CSS_OSC_DETECT\n");
		return -EIO;
	}
	if (status & NI67XX_CAL_STATUS_OVERRANGE) {
		dev_err(dev->class_dev,
			"cs5529 conversion error, overrange (ignoring)\n");
	}
	if (data) {
		*data = ni_ao_win_inw(dev, NI67XX_CAL_DATA_REG);
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

	/*
	 * Set calibration adc source.  Docs lie, reference select bits 8 to 11
	 * do nothing. bit 12 seems to chooses internal reference voltage, bit
	 * 13 causes the adc input to go overrange (maybe reads external
	 * reference?)
	 */
	if (insn->chanspec & CR_ALT_SOURCE)
		channel_select = INTERNAL_REF;
	else
		channel_select = CR_CHAN(insn->chanspec);
	ni_ao_win_outw(dev, channel_select, NI67XX_AO_CAL_CHAN_SEL_REG);

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
	ni_ao_win_outw(dev, (value >> 16) & 0xff, NI67XX_CAL_CFG_HI_REG);
	ni_ao_win_outw(dev, value & 0xffff, NI67XX_CAL_CFG_LO_REG);
	reg_select_bits &= CS5529_CMD_REG_MASK;
	cs5529_command(dev, CS5529_CMD_CB | reg_select_bits);
	if (cs5529_wait_for_idle(dev))
		dev_err(dev->class_dev,
			"timeout or signal in %s\n", __func__);
}

static int init_cs5529(struct comedi_device *dev)
{
	unsigned int config_bits = CS5529_CFG_PORT_FLAG |
				   CS5529_CFG_WORD_RATE_2180;

#if 1
	/* do self-calibration */
	cs5529_config_write(dev, config_bits | CS5529_CFG_CALIB_BOTH_SELF,
			    CS5529_CFG_REG);
	/* need to force a conversion for calibration to run */
	cs5529_do_conversion(dev, NULL);
#else
	/* force gain calibration to 1 */
	cs5529_config_write(dev, 0x400000, CS5529_GAIN_REG);
	cs5529_config_write(dev, config_bits | CS5529_CFG_CALIB_OFFSET_SELF,
			    CS5529_CFG_REG);
	if (cs5529_wait_for_idle(dev))
		dev_err(dev->class_dev,
			"timeout or signal in %s\n", __func__);
#endif
	return 0;
}

/*
 * Find best multiplier/divider to try and get the PLL running at 80 MHz
 * given an arbitrary frequency input clock.
 */
static int ni_mseries_get_pll_parameters(unsigned int reference_period_ns,
					 unsigned int *freq_divider,
					 unsigned int *freq_multiplier,
					 unsigned int *actual_period_ns)
{
	unsigned int div;
	unsigned int best_div = 1;
	unsigned int mult;
	unsigned int best_mult = 1;
	static const unsigned int pico_per_nano = 1000;
	const unsigned int reference_picosec = reference_period_ns *
					       pico_per_nano;
	/*
	 * m-series wants the phased-locked loop to output 80MHz, which is
	 * divided by 4 to 20 MHz for most timing clocks
	 */
	static const unsigned int target_picosec = 12500;
	int best_period_picosec = 0;

	for (div = 1; div <= NI_M_PLL_MAX_DIVISOR; ++div) {
		for (mult = 1; mult <= NI_M_PLL_MAX_MULTIPLIER; ++mult) {
			unsigned int new_period_ps =
			    (reference_picosec * div) / mult;
			if (abs(new_period_ps - target_picosec) <
			    abs(best_period_picosec - target_picosec)) {
				best_period_picosec = new_period_ps;
				best_div = div;
				best_mult = mult;
			}
		}
	}
	if (best_period_picosec == 0)
		return -EIO;

	*freq_divider = best_div;
	*freq_multiplier = best_mult;
	/* return the actual period (* fudge factor for 80 to 20 MHz) */
	*actual_period_ns = DIV_ROUND_CLOSEST(best_period_picosec * 4,
					      pico_per_nano);
	return 0;
}

static int ni_mseries_set_pll_master_clock(struct comedi_device *dev,
					   unsigned int source,
					   unsigned int period_ns)
{
	struct ni_private *devpriv = dev->private;
	static const unsigned int min_period_ns = 50;
	static const unsigned int max_period_ns = 1000;
	static const unsigned int timeout = 1000;
	unsigned int pll_control_bits;
	unsigned int freq_divider;
	unsigned int freq_multiplier;
	unsigned int rtsi;
	unsigned int i;
	int retval;

	if (source == NI_MIO_PLL_PXI10_CLOCK)
		period_ns = 100;
	/*
	 * These limits are somewhat arbitrary, but NI advertises 1 to 20MHz
	 * range so we'll use that.
	 */
	if (period_ns < min_period_ns || period_ns > max_period_ns) {
		dev_err(dev->class_dev,
			"%s: you must specify an input clock frequency between %i and %i nanosec for the phased-lock loop\n",
			__func__, min_period_ns, max_period_ns);
		return -EINVAL;
	}
	devpriv->rtsi_trig_direction_reg &= ~NISTC_RTSI_TRIG_USE_CLK;
	ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
		      NISTC_RTSI_TRIG_DIR_REG);
	pll_control_bits = NI_M_PLL_CTRL_ENA | NI_M_PLL_CTRL_VCO_MODE_75_150MHZ;
	devpriv->clock_and_fout2 |= NI_M_CLK_FOUT2_TIMEBASE1_PLL |
				    NI_M_CLK_FOUT2_TIMEBASE3_PLL;
	devpriv->clock_and_fout2 &= ~NI_M_CLK_FOUT2_PLL_SRC_MASK;
	switch (source) {
	case NI_MIO_PLL_PXI_STAR_TRIGGER_CLOCK:
		devpriv->clock_and_fout2 |= NI_M_CLK_FOUT2_PLL_SRC_STAR;
		break;
	case NI_MIO_PLL_PXI10_CLOCK:
		/* pxi clock is 10MHz */
		devpriv->clock_and_fout2 |= NI_M_CLK_FOUT2_PLL_SRC_PXI10;
		break;
	default:
		for (rtsi = 0; rtsi <= NI_M_MAX_RTSI_CHAN; ++rtsi) {
			if (source == NI_MIO_PLL_RTSI_CLOCK(rtsi)) {
				devpriv->clock_and_fout2 |=
					NI_M_CLK_FOUT2_PLL_SRC_RTSI(rtsi);
				break;
			}
		}
		if (rtsi > NI_M_MAX_RTSI_CHAN)
			return -EINVAL;
		break;
	}
	retval = ni_mseries_get_pll_parameters(period_ns,
					       &freq_divider,
					       &freq_multiplier,
					       &devpriv->clock_ns);
	if (retval < 0) {
		dev_err(dev->class_dev,
			"bug, failed to find pll parameters\n");
		return retval;
	}

	ni_writew(dev, devpriv->clock_and_fout2, NI_M_CLK_FOUT2_REG);
	pll_control_bits |= NI_M_PLL_CTRL_DIVISOR(freq_divider) |
			    NI_M_PLL_CTRL_MULTIPLIER(freq_multiplier);

	ni_writew(dev, pll_control_bits, NI_M_PLL_CTRL_REG);
	devpriv->clock_source = source;
	/* it takes a few hundred microseconds for PLL to lock */
	for (i = 0; i < timeout; ++i) {
		if (ni_readw(dev, NI_M_PLL_STATUS_REG) & NI_M_PLL_STATUS_LOCKED)
			break;
		udelay(1);
	}
	if (i == timeout) {
		dev_err(dev->class_dev,
			"%s: timed out waiting for PLL to lock to reference clock source %i with period %i ns\n",
			__func__, source, period_ns);
		return -ETIMEDOUT;
	}
	return 3;
}

static int ni_set_master_clock(struct comedi_device *dev,
			       unsigned int source, unsigned int period_ns)
{
	struct ni_private *devpriv = dev->private;

	if (source == NI_MIO_INTERNAL_CLOCK) {
		devpriv->rtsi_trig_direction_reg &= ~NISTC_RTSI_TRIG_USE_CLK;
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      NISTC_RTSI_TRIG_DIR_REG);
		devpriv->clock_ns = TIMEBASE_1_NS;
		if (devpriv->is_m_series) {
			devpriv->clock_and_fout2 &=
			    ~(NI_M_CLK_FOUT2_TIMEBASE1_PLL |
			      NI_M_CLK_FOUT2_TIMEBASE3_PLL);
			ni_writew(dev, devpriv->clock_and_fout2,
				  NI_M_CLK_FOUT2_REG);
			ni_writew(dev, 0, NI_M_PLL_CTRL_REG);
		}
		devpriv->clock_source = source;
	} else {
		if (devpriv->is_m_series) {
			return ni_mseries_set_pll_master_clock(dev, source,
							       period_ns);
		} else {
			if (source == NI_MIO_RTSI_CLOCK) {
				devpriv->rtsi_trig_direction_reg |=
				    NISTC_RTSI_TRIG_USE_CLK;
				ni_stc_writew(dev,
					      devpriv->rtsi_trig_direction_reg,
					      NISTC_RTSI_TRIG_DIR_REG);
				if (period_ns == 0) {
					dev_err(dev->class_dev,
						"we don't handle an unspecified clock period correctly yet, returning error\n");
					return -EINVAL;
				}
				devpriv->clock_ns = period_ns;
				devpriv->clock_source = source;
			} else {
				return -EINVAL;
			}
		}
	}
	return 3;
}

static int ni_valid_rtsi_output_source(struct comedi_device *dev,
				       unsigned int chan, unsigned int source)
{
	struct ni_private *devpriv = dev->private;

	if (chan >= NISTC_RTSI_TRIG_NUM_CHAN(devpriv->is_m_series)) {
		if (chan == NISTC_RTSI_TRIG_OLD_CLK_CHAN) {
			if (source == NI_RTSI_OUTPUT_RTSI_OSC)
				return 1;

			dev_err(dev->class_dev,
				"%s: invalid source for channel=%i, channel %i is always the RTSI clock for pre-m-series boards\n",
				__func__, chan, NISTC_RTSI_TRIG_OLD_CLK_CHAN);
			return 0;
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
	case NI_RTSI_OUTPUT_RTSI_OSC:
		return (devpriv->is_m_series) ? 1 : 0;
	default:
		return 0;
	}
}

static int ni_set_rtsi_routing(struct comedi_device *dev,
			       unsigned int chan, unsigned int src)
{
	struct ni_private *devpriv = dev->private;

	if (ni_valid_rtsi_output_source(dev, chan, src) == 0)
		return -EINVAL;
	if (chan < 4) {
		devpriv->rtsi_trig_a_output_reg &= ~NISTC_RTSI_TRIG_MASK(chan);
		devpriv->rtsi_trig_a_output_reg |= NISTC_RTSI_TRIG(chan, src);
		ni_stc_writew(dev, devpriv->rtsi_trig_a_output_reg,
			      NISTC_RTSI_TRIGA_OUT_REG);
	} else if (chan < 8) {
		devpriv->rtsi_trig_b_output_reg &= ~NISTC_RTSI_TRIG_MASK(chan);
		devpriv->rtsi_trig_b_output_reg |= NISTC_RTSI_TRIG(chan, src);
		ni_stc_writew(dev, devpriv->rtsi_trig_b_output_reg,
			      NISTC_RTSI_TRIGB_OUT_REG);
	}
	return 2;
}

static unsigned int ni_get_rtsi_routing(struct comedi_device *dev,
					unsigned int chan)
{
	struct ni_private *devpriv = dev->private;

	if (chan < 4) {
		return NISTC_RTSI_TRIG_TO_SRC(chan,
					      devpriv->rtsi_trig_a_output_reg);
	} else if (chan < NISTC_RTSI_TRIG_NUM_CHAN(devpriv->is_m_series)) {
		return NISTC_RTSI_TRIG_TO_SRC(chan,
					      devpriv->rtsi_trig_b_output_reg);
	} else {
		if (chan == NISTC_RTSI_TRIG_OLD_CLK_CHAN)
			return NI_RTSI_OUTPUT_RTSI_OSC;
		dev_err(dev->class_dev, "bug! should never get here?\n");
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
	unsigned int max_chan = NISTC_RTSI_TRIG_NUM_CHAN(devpriv->is_m_series);

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		if (chan < max_chan) {
			devpriv->rtsi_trig_direction_reg |=
			    NISTC_RTSI_TRIG_DIR(chan, devpriv->is_m_series);
		} else if (chan == NISTC_RTSI_TRIG_OLD_CLK_CHAN) {
			devpriv->rtsi_trig_direction_reg |=
			    NISTC_RTSI_TRIG_DRV_CLK;
		}
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      NISTC_RTSI_TRIG_DIR_REG);
		break;
	case INSN_CONFIG_DIO_INPUT:
		if (chan < max_chan) {
			devpriv->rtsi_trig_direction_reg &=
			    ~NISTC_RTSI_TRIG_DIR(chan, devpriv->is_m_series);
		} else if (chan == NISTC_RTSI_TRIG_OLD_CLK_CHAN) {
			devpriv->rtsi_trig_direction_reg &=
			    ~NISTC_RTSI_TRIG_DRV_CLK;
		}
		ni_stc_writew(dev, devpriv->rtsi_trig_direction_reg,
			      NISTC_RTSI_TRIG_DIR_REG);
		break;
	case INSN_CONFIG_DIO_QUERY:
		if (chan < max_chan) {
			data[1] =
			    (devpriv->rtsi_trig_direction_reg &
			     NISTC_RTSI_TRIG_DIR(chan, devpriv->is_m_series))
				? INSN_CONFIG_DIO_OUTPUT
				: INSN_CONFIG_DIO_INPUT;
		} else if (chan == NISTC_RTSI_TRIG_OLD_CLK_CHAN) {
			data[1] = (devpriv->rtsi_trig_direction_reg &
				   NISTC_RTSI_TRIG_DRV_CLK)
				  ? INSN_CONFIG_DIO_OUTPUT
				  : INSN_CONFIG_DIO_INPUT;
		}
		return 2;
	case INSN_CONFIG_SET_CLOCK_SRC:
		return ni_set_master_clock(dev, data[1], data[2]);
	case INSN_CONFIG_GET_CLOCK_SRC:
		data[1] = devpriv->clock_source;
		data[2] = devpriv->clock_ns;
		return 3;
	case INSN_CONFIG_SET_ROUTING:
		return ni_set_rtsi_routing(dev, chan, data[1]);
	case INSN_CONFIG_GET_ROUTING:
		data[1] = ni_get_rtsi_routing(dev, chan);
		return 2;
	default:
		return -EINVAL;
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

	/*
	 * Use 10MHz instead of 20MHz for RTSI clock frequency. Appears
	 * to have no effect, at least on pxi-6281, which always uses
	 * 20MHz rtsi clock frequency
	 */
	devpriv->clock_and_fout2 = NI_M_CLK_FOUT2_RTSI_10MHZ;
	/*  Set clock mode to internal */
	if (ni_set_master_clock(dev, NI_MIO_INTERNAL_CLOCK, 0) < 0)
		dev_err(dev->class_dev, "ni_set_master_clock failed, bug?\n");
	/*  default internal lines routing to RTSI bus lines */
	devpriv->rtsi_trig_a_output_reg =
	    NISTC_RTSI_TRIG(0, NI_RTSI_OUTPUT_ADR_START1) |
	    NISTC_RTSI_TRIG(1, NI_RTSI_OUTPUT_ADR_START2) |
	    NISTC_RTSI_TRIG(2, NI_RTSI_OUTPUT_SCLKG) |
	    NISTC_RTSI_TRIG(3, NI_RTSI_OUTPUT_DACUPDN);
	ni_stc_writew(dev, devpriv->rtsi_trig_a_output_reg,
		      NISTC_RTSI_TRIGA_OUT_REG);
	devpriv->rtsi_trig_b_output_reg =
	    NISTC_RTSI_TRIG(4, NI_RTSI_OUTPUT_DA_START1) |
	    NISTC_RTSI_TRIG(5, NI_RTSI_OUTPUT_G_SRC0) |
	    NISTC_RTSI_TRIG(6, NI_RTSI_OUTPUT_G_GATE0);
	if (devpriv->is_m_series)
		devpriv->rtsi_trig_b_output_reg |=
		    NISTC_RTSI_TRIG(7, NI_RTSI_OUTPUT_RTSI_OSC);
	ni_stc_writew(dev, devpriv->rtsi_trig_b_output_reg,
		      NISTC_RTSI_TRIGB_OUT_REG);

	/*
	 * Sets the source and direction of the 4 on board lines
	 * ni_stc_writew(dev, 0, NISTC_RTSI_BOARD_REG);
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
		dev_err(dev->class_dev,
			"no dma channel available for use by counter\n");
		return retval;
	}
	ni_tio_acknowledge(counter);
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

static irqreturn_t ni_E_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s_ai = dev->read_subdev;
	struct comedi_subdevice *s_ao = dev->write_subdev;
	unsigned short a_status;
	unsigned short b_status;
	unsigned long flags;
#ifdef PCIDMA
	struct ni_private *devpriv = dev->private;
#endif

	if (!dev->attached)
		return IRQ_NONE;
	smp_mb();		/* make sure dev->attached is checked */

	/*  lock to avoid race with comedi_poll */
	spin_lock_irqsave(&dev->spinlock, flags);
	a_status = ni_stc_readw(dev, NISTC_AI_STATUS1_REG);
	b_status = ni_stc_readw(dev, NISTC_AO_STATUS1_REG);
#ifdef PCIDMA
	if (devpriv->mite) {
		unsigned long flags_too;

		spin_lock_irqsave(&devpriv->mite_channel_lock, flags_too);
		if (s_ai && devpriv->ai_mite_chan)
			mite_ack_linkc(devpriv->ai_mite_chan, s_ai, false);
		if (s_ao && devpriv->ao_mite_chan)
			mite_ack_linkc(devpriv->ao_mite_chan, s_ao, false);
		spin_unlock_irqrestore(&devpriv->mite_channel_lock, flags_too);
	}
#endif
	ack_a_interrupt(dev, a_status);
	ack_b_interrupt(dev, b_status);
	if (s_ai) {
		if (a_status & NISTC_AI_STATUS1_INTA)
			handle_a_interrupt(dev, s_ai, a_status);
		/* handle any interrupt or dma events */
		comedi_handle_events(dev, s_ai);
	}
	if (s_ao) {
		if (b_status & NISTC_AO_STATUS1_INTB)
			handle_b_interrupt(dev, s_ao, b_status);
		/* handle any interrupt or dma events */
		comedi_handle_events(dev, s_ao);
	}
	handle_gpct_interrupt(dev, 0);
	handle_gpct_interrupt(dev, 1);
#ifdef PCIDMA
	if (devpriv->is_m_series)
		handle_cdio_interrupt(dev);
#endif

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
		     unsigned int interrupt_pin, unsigned int irq_polarity)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;
	struct comedi_subdevice *s;
	int ret;
	int i;

	if (board->n_aochan > MAX_N_AO_CHAN) {
		dev_err(dev->class_dev, "bug! n_aochan > MAX_N_AO_CHAN\n");
		return -EINVAL;
	}

	/* initialize clock dividers */
	devpriv->clock_and_fout = NISTC_CLK_FOUT_SLOW_DIV2 |
				  NISTC_CLK_FOUT_SLOW_TIMEBASE |
				  NISTC_CLK_FOUT_TO_BOARD_DIV2 |
				  NISTC_CLK_FOUT_TO_BOARD;
	if (!devpriv->is_6xxx) {
		/* BEAM is this needed for PCI-6143 ?? */
		devpriv->clock_and_fout |= (NISTC_CLK_FOUT_AI_OUT_DIV2 |
					    NISTC_CLK_FOUT_AO_OUT_DIV2);
	}
	ni_stc_writew(dev, devpriv->clock_and_fout, NISTC_CLK_FOUT_REG);

	ret = comedi_alloc_subdevices(dev, NI_NUM_SUBDEVICES);
	if (ret)
		return ret;

	/* Analog Input subdevice */
	s = &dev->subdevices[NI_AI_SUBDEV];
	if (board->n_adchan) {
		s->type		= COMEDI_SUBD_AI;
		s->subdev_flags	= SDF_READABLE | SDF_DIFF | SDF_DITHER;
		if (!devpriv->is_611x)
			s->subdev_flags	|= SDF_GROUND | SDF_COMMON | SDF_OTHER;
		if (board->ai_maxdata > 0xffff)
			s->subdev_flags	|= SDF_LSAMPL;
		if (devpriv->is_m_series)
			s->subdev_flags	|= SDF_SOFT_CALIBRATED;
		s->n_chan	= board->n_adchan;
		s->maxdata	= board->ai_maxdata;
		s->range_table	= ni_range_lkup[board->gainlkup];
		s->insn_read	= ni_ai_insn_read;
		s->insn_config	= ni_ai_insn_config;
		if (dev->irq) {
			dev->read_subdev = s;
			s->subdev_flags	|= SDF_CMD_READ;
			s->len_chanlist	= 512;
			s->do_cmdtest	= ni_ai_cmdtest;
			s->do_cmd	= ni_ai_cmd;
			s->cancel	= ni_ai_reset;
			s->poll		= ni_ai_poll;
			s->munge	= ni_ai_munge;

			if (devpriv->mite)
				s->async_dma_dir = DMA_FROM_DEVICE;
		}

		/* reset the analog input configuration */
		ni_ai_reset(dev, s);
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Analog Output subdevice */
	s = &dev->subdevices[NI_AO_SUBDEV];
	if (board->n_aochan) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE | SDF_DEGLITCH | SDF_GROUND;
		if (devpriv->is_m_series)
			s->subdev_flags	|= SDF_SOFT_CALIBRATED;
		s->n_chan	= board->n_aochan;
		s->maxdata	= board->ao_maxdata;
		s->range_table	= board->ao_range_table;
		s->insn_config	= ni_ao_insn_config;
		s->insn_write	= ni_ao_insn_write;

		ret = comedi_alloc_subdev_readback(s);
		if (ret)
			return ret;

		/*
		 * Along with the IRQ we need either a FIFO or DMA for
		 * async command support.
		 */
		if (dev->irq && (board->ao_fifo_depth || devpriv->mite)) {
			dev->write_subdev = s;
			s->subdev_flags	|= SDF_CMD_WRITE;
			s->len_chanlist	= s->n_chan;
			s->do_cmdtest	= ni_ao_cmdtest;
			s->do_cmd	= ni_ao_cmd;
			s->cancel	= ni_ao_reset;
			if (!devpriv->is_m_series)
				s->munge	= ni_ao_munge;

			if (devpriv->mite)
				s->async_dma_dir = DMA_TO_DEVICE;
		}

		if (devpriv->is_67xx)
			init_ao_67xx(dev, s);

		/* reset the analog output configuration */
		ni_ao_reset(dev, s);
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	/* Digital I/O subdevice */
	s = &dev->subdevices[NI_DIO_SUBDEV];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_WRITABLE | SDF_READABLE;
	s->n_chan	= board->has_32dio_chan ? 32 : 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	if (devpriv->is_m_series) {
#ifdef PCIDMA
		s->subdev_flags	|= SDF_LSAMPL;
		s->insn_bits	= ni_m_series_dio_insn_bits;
		s->insn_config	= ni_m_series_dio_insn_config;
		if (dev->irq) {
			s->subdev_flags	|= SDF_CMD_WRITE /* | SDF_CMD_READ */;
			s->len_chanlist	= s->n_chan;
			s->do_cmdtest	= ni_cdio_cmdtest;
			s->do_cmd	= ni_cdio_cmd;
			s->cancel	= ni_cdio_cancel;

			/* M-series boards use DMA */
			s->async_dma_dir = DMA_BIDIRECTIONAL;
		}

		/* reset DIO and set all channels to inputs */
		ni_writel(dev, NI_M_CDO_CMD_RESET |
			       NI_M_CDI_CMD_RESET,
			  NI_M_CDIO_CMD_REG);
		ni_writel(dev, s->io_bits, NI_M_DIO_DIR_REG);
#endif /* PCIDMA */
	} else {
		s->insn_bits	= ni_dio_insn_bits;
		s->insn_config	= ni_dio_insn_config;

		/* set all channels to inputs */
		devpriv->dio_control = NISTC_DIO_CTRL_DIR(s->io_bits);
		ni_writew(dev, devpriv->dio_control, NISTC_DIO_CTRL_REG);
	}

	/* 8255 device */
	s = &dev->subdevices[NI_8255_DIO_SUBDEV];
	if (board->has_8255) {
		ret = subdev_8255_init(dev, s, ni_8255_callback,
				       NI_E_8255_BASE);
		if (ret)
			return ret;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* formerly general purpose counter/timer device, but no longer used */
	s = &dev->subdevices[NI_UNUSED_SUBDEV];
	s->type = COMEDI_SUBD_UNUSED;

	/* Calibration subdevice */
	s = &dev->subdevices[NI_CALIBRATION_SUBDEV];
	s->type		= COMEDI_SUBD_CALIB;
	s->subdev_flags	= SDF_INTERNAL;
	s->n_chan	= 1;
	s->maxdata	= 0;
	if (devpriv->is_m_series) {
		/* internal PWM output used for AI nonlinearity calibration */
		s->insn_config	= ni_m_series_pwm_config;

		ni_writel(dev, 0x0, NI_M_CAL_PWM_REG);
	} else if (devpriv->is_6143) {
		/* internal PWM output used for AI nonlinearity calibration */
		s->insn_config	= ni_6143_pwm_config;
	} else {
		s->subdev_flags	|= SDF_WRITABLE;
		s->insn_read	= ni_calib_insn_read;
		s->insn_write	= ni_calib_insn_write;

		/* setup the caldacs and find the real n_chan and maxdata */
		caldac_setup(dev, s);
	}

	/* EEPROM subdevice */
	s = &dev->subdevices[NI_EEPROM_SUBDEV];
	s->type		= COMEDI_SUBD_MEMORY;
	s->subdev_flags	= SDF_READABLE | SDF_INTERNAL;
	s->maxdata	= 0xff;
	if (devpriv->is_m_series) {
		s->n_chan	= M_SERIES_EEPROM_SIZE;
		s->insn_read	= ni_m_series_eeprom_insn_read;
	} else {
		s->n_chan	= 512;
		s->insn_read	= ni_eeprom_insn_read;
	}

	/* Digital I/O (PFI) subdevice */
	s = &dev->subdevices[NI_PFI_DIO_SUBDEV];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_INTERNAL;
	s->maxdata	= 1;
	if (devpriv->is_m_series) {
		s->n_chan	= 16;
		s->insn_bits	= ni_pfi_insn_bits;

		ni_writew(dev, s->state, NI_M_PFI_DO_REG);
		for (i = 0; i < NUM_PFI_OUTPUT_SELECT_REGS; ++i) {
			ni_writew(dev, devpriv->pfi_output_select_reg[i],
				  NI_M_PFI_OUT_SEL_REG(i));
		}
	} else {
		s->n_chan	= 10;
	}
	s->insn_config	= ni_pfi_insn_config;

	ni_set_bits(dev, NISTC_IO_BIDIR_PIN_REG, ~0, 0);

	/* cs5529 calibration adc */
	s = &dev->subdevices[NI_CS5529_CALIBRATION_SUBDEV];
	if (devpriv->is_67xx) {
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

	/* allocate and initialize the gpct counter device */
	devpriv->counter_dev = ni_gpct_device_construct(dev,
					ni_gpct_write_register,
					ni_gpct_read_register,
					(devpriv->is_m_series)
						? ni_gpct_variant_m_series
						: ni_gpct_variant_e_series,
					NUM_GPCT);
	if (!devpriv->counter_dev)
		return -ENOMEM;

	/* Counter (gpct) subdevices */
	for (i = 0; i < NUM_GPCT; ++i) {
		struct ni_gpct *gpct = &devpriv->counter_dev->counters[i];

		/* setup and initialize the counter */
		gpct->chip_index = 0;
		gpct->counter_index = i;
		ni_tio_init_counter(gpct);

		s = &dev->subdevices[NI_GPCT_SUBDEV(i)];
		s->type		= COMEDI_SUBD_COUNTER;
		s->subdev_flags	= SDF_READABLE | SDF_WRITABLE | SDF_LSAMPL;
		s->n_chan	= 3;
		s->maxdata	= (devpriv->is_m_series) ? 0xffffffff
							 : 0x00ffffff;
		s->insn_read	= ni_tio_insn_read;
		s->insn_write	= ni_tio_insn_read;
		s->insn_config	= ni_tio_insn_config;
#ifdef PCIDMA
		if (dev->irq && devpriv->mite) {
			s->subdev_flags	|= SDF_CMD_READ /* | SDF_CMD_WRITE */;
			s->len_chanlist	= 1;
			s->do_cmdtest	= ni_tio_cmdtest;
			s->do_cmd	= ni_gpct_cmd;
			s->cancel	= ni_gpct_cancel;

			s->async_dma_dir = DMA_BIDIRECTIONAL;
		}
#endif
		s->private	= gpct;
	}

	/* Frequency output subdevice */
	s = &dev->subdevices[NI_FREQ_OUT_SUBDEV];
	s->type		= COMEDI_SUBD_COUNTER;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 1;
	s->maxdata	= 0xf;
	s->insn_read	= ni_freq_out_insn_read;
	s->insn_write	= ni_freq_out_insn_write;
	s->insn_config	= ni_freq_out_insn_config;

	if (dev->irq) {
		ni_stc_writew(dev,
			      (irq_polarity ? NISTC_INT_CTRL_INT_POL : 0) |
			      (NISTC_INT_CTRL_3PIN_INT & 0) |
			      NISTC_INT_CTRL_INTA_ENA |
			      NISTC_INT_CTRL_INTB_ENA |
			      NISTC_INT_CTRL_INTA_SEL(interrupt_pin) |
			      NISTC_INT_CTRL_INTB_SEL(interrupt_pin),
			      NISTC_INT_CTRL_REG);
	}

	/* DMA setup */
	ni_writeb(dev, devpriv->ai_ao_select_reg, NI_E_DMA_AI_AO_SEL_REG);
	ni_writeb(dev, devpriv->g0_g1_select_reg, NI_E_DMA_G0_G1_SEL_REG);

	if (devpriv->is_6xxx) {
		ni_writeb(dev, 0, NI611X_MAGIC_REG);
	} else if (devpriv->is_m_series) {
		int channel;

		for (channel = 0; channel < board->n_aochan; ++channel) {
			ni_writeb(dev, 0xf,
				  NI_M_AO_WAVEFORM_ORDER_REG(channel));
			ni_writeb(dev, 0x0,
				  NI_M_AO_REF_ATTENUATION_REG(channel));
		}
		ni_writeb(dev, 0x0, NI_M_AO_CALIB_REG);
	}

	return 0;
}

static void mio_common_detach(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	if (devpriv)
		ni_gpct_device_destroy(devpriv->counter_dev);
}
