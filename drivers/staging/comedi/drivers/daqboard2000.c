/*
 * comedi/drivers/daqboard2000.c
 * hardware driver for IOtech DAQboard/2000
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>
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
 * Driver: daqboard2000
 * Description: IOTech DAQBoard/2000
 * Author: Anders Blomdell <anders.blomdell@control.lth.se>
 * Status: works
 * Updated: Mon, 14 Apr 2008 15:28:52 +0100
 * Devices: [IOTech] DAQBoard/2000 (daqboard2000)
 *
 * Much of the functionality of this driver was determined from reading
 * the source code for the Windows driver.
 *
 * The FPGA on the board requires firmware, which is available from
 * http://www.comedi.org in the comedi_nonfree_firmware tarball.
 *
 * Configuration options: not applicable, uses PCI auto config
 */
/*
 * This card was obviously never intended to leave the Windows world,
 * since it lacked all kind of hardware documentation (except for cable
 * pinouts, plug and pray has something to catch up with yet).
 *
 * With some help from our swedish distributor, we got the Windows sourcecode
 * for the card, and here are the findings so far.
 *
 * 1. A good document that describes the PCI interface chip is 9080db-106.pdf
 *    available from http://www.plxtech.com/products/io/pci9080
 *
 * 2. The initialization done so far is:
 *      a. program the FPGA (windows code sans a lot of error messages)
 *      b.
 *
 * 3. Analog out seems to work OK with DAC's disabled, if DAC's are enabled,
 *    you have to output values to all enabled DAC's until result appears, I
 *    guess that it has something to do with pacer clocks, but the source
 *    gives me no clues. I'll keep it simple so far.
 *
 * 4. Analog in.
 *    Each channel in the scanlist seems to be controlled by four
 *    control words:
 *
 *	Word0:
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	  ! | | | ! | | | ! | | | ! | | | !
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	Word1:
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	  ! | | | ! | | | ! | | | ! | | | !
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	   |             |       | | | | |
 *	   +------+------+       | | | | +-- Digital input (??)
 *		  |		 | | | +---- 10 us settling time
 *		  |		 | | +------ Suspend acquisition (last to scan)
 *		  |		 | +-------- Simultaneous sample and hold
 *		  |		 +---------- Signed data format
 *		  +------------------------- Correction offset low
 *
 *	Word2:
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	  ! | | | ! | | | ! | | | ! | | | !
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	   |     | |     | | | | | |     |
 *	   +-----+ +--+--+ +++ +++ +--+--+
 *	      |       |     |   |     +----- Expansion channel
 *	      |       |     |   +----------- Expansion gain
 *	      |       |     +--------------- Channel (low)
 *	      |       +--------------------- Correction offset high
 *	      +----------------------------- Correction gain low
 *	Word3:
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	  ! | | | ! | | | ! | | | ! | | | !
 *	  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *	   |             | | | |   | | | |
 *	   +------+------+ | | +-+-+ | | +-- Low bank enable
 *		  |	   | |   |   | +---- High bank enable
 *		  |	   | |   |   +------ Hi/low select
 *		  |	   | |   +---------- Gain (1,?,2,4,8,16,32,64)
 *		  |	   | +-------------- differential/single ended
 *		  |	   +---------------- Unipolar
 *		  +------------------------- Correction gain high
 *
 * 999. The card seems to have an incredible amount of capabilities, but
 *      trying to reverse engineer them from the Windows source is beyond my
 *      patience.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "8255.h"

#define DAQBOARD2000_FIRMWARE		"daqboard2000_firmware.bin"

#define DAQBOARD2000_SUBSYSTEM_IDS2	0x0002	/* Daqboard/2000 - 2 Dacs */
#define DAQBOARD2000_SUBSYSTEM_IDS4	0x0004	/* Daqboard/2000 - 4 Dacs */

/* Initialization bits for the Serial EEPROM Control Register */
#define DB2K_SECR_PROG_PIN_HI		0x8001767e
#define DB2K_SECR_PROG_PIN_LO		0x8000767e
#define DB2K_SECR_LOCAL_BUS_HI		0xc000767e
#define DB2K_SECR_LOCAL_BUS_LO		0x8000767e
#define DB2K_SECR_RELOAD_HI		0xa000767e
#define DB2K_SECR_RELOAD_LO		0x8000767e

/* SECR status bits */
#define DAQBOARD2000_EEPROM_PRESENT     0x10000000

/* CPLD status bits */
#define DAQBOARD2000_CPLD_INIT		0x0002
#define DAQBOARD2000_CPLD_DONE		0x0004

static const struct comedi_lrange range_daqboard2000_ai = {
	13, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
		BIP_RANGE(0.625),
		BIP_RANGE(0.3125),
		BIP_RANGE(0.156),
		UNI_RANGE(10),
		UNI_RANGE(5),
		UNI_RANGE(2.5),
		UNI_RANGE(1.25),
		UNI_RANGE(0.625),
		UNI_RANGE(0.3125)
	}
};

/*
 * Register Memory Map
 */
#define DB2K_REG_ACQ_CONTROL			0x00		/* u16 (w) */
#define DB2K_REG_ACQ_STATUS			0x00		/* u16 (r) */
#define DB2K_REG_ACQ_SCAN_LIST_FIFO		0x02		/* u16 */
#define DB2K_REG_ACQ_PACER_CLOCK_DIV_LOW	0x04		/* u32 */
#define DB2K_REG_ACQ_SCAN_COUNTER		0x08		/* u16 */
#define DB2K_REG_ACQ_PACER_CLOCK_DIV_HIGH	0x0a		/* u16 */
#define DB2K_REG_ACQ_TRIGGER_COUNT		0x0c		/* u16 */
#define DB2K_REG_ACQ_RESULTS_FIFO		0x10		/* u16 */
#define DB2K_REG_ACQ_RESULTS_SHADOW		0x14		/* u16 */
#define DB2K_REG_ACQ_ADC_RESULT			0x18		/* u16 */
#define DB2K_REG_DAC_SCAN_COUNTER		0x1c		/* u16 */
#define DB2K_REG_DAC_CONTROL			0x20		/* u16 (w) */
#define DB2K_REG_DAC_STATUS			0x20		/* u16 (r) */
#define DB2K_REG_DAC_FIFO			0x24		/* s16 */
#define DB2K_REG_DAC_PACER_CLOCK_DIV		0x2a		/* u16 */
#define DB2K_REG_REF_DACS			0x2c		/* u16 */
#define DB2K_REG_DIO_CONTROL			0x30		/* u16 */
#define DB2K_REG_P3_HSIO_DATA			0x32		/* s16 */
#define DB2K_REG_P3_CONTROL			0x34		/* u16 */
#define DB2K_REG_CAL_EEPROM_CONTROL		0x36		/* u16 */
#define DB2K_REG_DAC_SETTING(x)			(0x38 + (x) * 2) /* s16 */
#define DB2K_REG_DIO_P2_EXP_IO_8_BIT		0x40		/* s16 */
#define DB2K_REG_COUNTER_TIMER_CONTROL		0x80		/* u16 */
#define DB2K_REG_COUNTER_INPUT(x)		(0x88 + (x) * 2) /* s16 */
#define DB2K_REG_TIMER_DIV(x)			(0xa0 + (x) * 2) /* u16 */
#define DB2K_REG_DMA_CONTROL			0xb0		/* u16 */
#define DB2K_REG_TRIG_CONTROL			0xb2		/* u16 */
#define DB2K_REG_CAL_EEPROM			0xb8		/* u16 */
#define DB2K_REG_ACQ_DIGITAL_MARK		0xba		/* u16 */
#define DB2K_REG_TRIG_DACS			0xbc		/* u16 */
#define DB2K_REG_DIO_P2_EXP_IO_16_BIT(x)	(0xc0 + (x) * 2) /* s16 */

/* Scan Sequencer programming */
#define DB2K_ACQ_CONTROL_SEQ_START_SCAN_LIST		0x0011
#define DB2K_ACQ_CONTROL_SEQ_STOP_SCAN_LIST		0x0010

/* Prepare for acquisition */
#define DB2K_ACQ_CONTROL_RESET_SCAN_LIST_FIFO		0x0004
#define DB2K_ACQ_CONTROL_RESET_RESULTS_FIFO		0x0002
#define DB2K_ACQ_CONTROL_RESET_CONFIG_PIPE		0x0001

/* Pacer Clock Control */
#define DB2K_ACQ_CONTROL_ADC_PACER_INTERNAL		0x0030
#define DB2K_ACQ_CONTROL_ADC_PACER_EXTERNAL		0x0032
#define DB2K_ACQ_CONTROL_ADC_PACER_ENABLE		0x0031
#define DB2K_ACQ_CONTROL_ADC_PACER_ENABLE_DAC_PACER	0x0034
#define DB2K_ACQ_CONTROL_ADC_PACER_DISABLE		0x0030
#define DB2K_ACQ_CONTROL_ADC_PACER_NORMAL_MODE		0x0060
#define DB2K_ACQ_CONTROL_ADC_PACER_COMPATIBILITY_MODE	0x0061
#define DB2K_ACQ_CONTROL_ADC_PACER_INTERNAL_OUT_ENABLE	0x0008
#define DB2K_ACQ_CONTROL_ADC_PACER_EXTERNAL_RISING	0x0100

/* Acquisition status bits */
#define DB2K_ACQ_STATUS_RESULTS_FIFO_MORE_1_SAMPLE	0x0001
#define DB2K_ACQ_STATUS_RESULTS_FIFO_HAS_DATA		0x0002
#define DB2K_ACQ_STATUS_RESULTS_FIFO_OVERRUN		0x0004
#define DB2K_ACQ_STATUS_LOGIC_SCANNING			0x0008
#define DB2K_ACQ_STATUS_CONFIG_PIPE_FULL		0x0010
#define DB2K_ACQ_STATUS_SCAN_LIST_FIFO_EMPTY		0x0020
#define DB2K_ACQ_STATUS_ADC_NOT_READY			0x0040
#define DB2K_ACQ_STATUS_ARBITRATION_FAILURE		0x0080
#define DB2K_ACQ_STATUS_ADC_PACER_OVERRUN		0x0100
#define DB2K_ACQ_STATUS_DAC_PACER_OVERRUN		0x0200

/* DAC status */
#define DB2K_DAC_STATUS_DAC_FULL			0x0001
#define DB2K_DAC_STATUS_REF_BUSY			0x0002
#define DB2K_DAC_STATUS_TRIG_BUSY			0x0004
#define DB2K_DAC_STATUS_CAL_BUSY			0x0008
#define DB2K_DAC_STATUS_DAC_BUSY(x)			(0x0010 << (x))

/* DAC control */
#define DB2K_DAC_CONTROL_ENABLE_BIT			0x0001
#define DB2K_DAC_CONTROL_DATA_IS_SIGNED			0x0002
#define DB2K_DAC_CONTROL_RESET_FIFO			0x0004
#define DB2K_DAC_CONTROL_DAC_DISABLE(x)			(0x0020 + ((x) << 4))
#define DB2K_DAC_CONTROL_DAC_ENABLE(x)			(0x0021 + ((x) << 4))
#define DB2K_DAC_CONTROL_PATTERN_DISABLE		0x0060
#define DB2K_DAC_CONTROL_PATTERN_ENABLE			0x0061

/* Trigger Control */
#define DB2K_TRIG_CONTROL_TYPE_ANALOG			0x0000
#define DB2K_TRIG_CONTROL_TYPE_TTL			0x0010
#define DB2K_TRIG_CONTROL_EDGE_HI_LO			0x0004
#define DB2K_TRIG_CONTROL_EDGE_LO_HI			0x0000
#define DB2K_TRIG_CONTROL_LEVEL_ABOVE			0x0000
#define DB2K_TRIG_CONTROL_LEVEL_BELOW			0x0004
#define DB2K_TRIG_CONTROL_SENSE_LEVEL			0x0002
#define DB2K_TRIG_CONTROL_SENSE_EDGE			0x0000
#define DB2K_TRIG_CONTROL_ENABLE			0x0001
#define DB2K_TRIG_CONTROL_DISABLE			0x0000

/* Reference Dac Selection */
#define DB2K_REF_DACS_SET				0x0080
#define DB2K_REF_DACS_SELECT_POS_REF			0x0100
#define DB2K_REF_DACS_SELECT_NEG_REF			0x0000

struct daq200_boardtype {
	const char *name;
	int id;
};

static const struct daq200_boardtype boardtypes[] = {
	{"ids2", DAQBOARD2000_SUBSYSTEM_IDS2},
	{"ids4", DAQBOARD2000_SUBSYSTEM_IDS4},
};

struct daqboard2000_private {
	enum {
		card_daqboard_2000
	} card;
	void __iomem *plx;
};

static void daqboard2000_write_acq_scan_list_entry(struct comedi_device *dev,
						   u16 entry)
{
	writew(entry & 0x00ff, dev->mmio + DB2K_REG_ACQ_SCAN_LIST_FIFO);
	writew((entry >> 8) & 0x00ff,
	       dev->mmio + DB2K_REG_ACQ_SCAN_LIST_FIFO);
}

static void daqboard2000_setup_sampling(struct comedi_device *dev, int chan,
					int gain)
{
	u16 word0, word1, word2, word3;

	/* Channel 0-7 diff, channel 8-23 single ended */
	word0 = 0;
	word1 = 0x0004;		/* Last scan */
	word2 = (chan << 6) & 0x00c0;
	switch (chan / 4) {
	case 0:
		word3 = 0x0001;
		break;
	case 1:
		word3 = 0x0002;
		break;
	case 2:
		word3 = 0x0005;
		break;
	case 3:
		word3 = 0x0006;
		break;
	case 4:
		word3 = 0x0041;
		break;
	case 5:
		word3 = 0x0042;
		break;
	default:
		word3 = 0;
		break;
	}
	/* These should be read from EEPROM */
	word2 |= 0x0800;	/* offset */
	word3 |= 0xc000;	/* gain */
	daqboard2000_write_acq_scan_list_entry(dev, word0);
	daqboard2000_write_acq_scan_list_entry(dev, word1);
	daqboard2000_write_acq_scan_list_entry(dev, word2);
	daqboard2000_write_acq_scan_list_entry(dev, word3);
}

static int daqboard2000_ai_status(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned long context)
{
	unsigned int status;

	status = readw(dev->mmio + DB2K_REG_ACQ_STATUS);
	if (status & context)
		return 0;
	return -EBUSY;
}

static int daqboard2000_ai_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	int gain, chan;
	int ret;
	int i;

	writew(DB2K_ACQ_CONTROL_RESET_SCAN_LIST_FIFO |
	       DB2K_ACQ_CONTROL_RESET_RESULTS_FIFO |
	       DB2K_ACQ_CONTROL_RESET_CONFIG_PIPE,
	       dev->mmio + DB2K_REG_ACQ_CONTROL);

	/*
	 * If pacer clock is not set to some high value (> 10 us), we
	 * risk multiple samples to be put into the result FIFO.
	 */
	/* 1 second, should be long enough */
	writel(1000000, dev->mmio + DB2K_REG_ACQ_PACER_CLOCK_DIV_LOW);
	writew(0, dev->mmio + DB2K_REG_ACQ_PACER_CLOCK_DIV_HIGH);

	gain = CR_RANGE(insn->chanspec);
	chan = CR_CHAN(insn->chanspec);

	/*
	 * This doesn't look efficient.  I decided to take the conservative
	 * approach when I did the insn conversion.  Perhaps it would be
	 * better to have broken it completely, then someone would have been
	 * forced to fix it.  --ds
	 */
	for (i = 0; i < insn->n; i++) {
		daqboard2000_setup_sampling(dev, chan, gain);
		/* Enable reading from the scanlist FIFO */
		writew(DB2K_ACQ_CONTROL_SEQ_START_SCAN_LIST,
		       dev->mmio + DB2K_REG_ACQ_CONTROL);

		ret = comedi_timeout(dev, s, insn, daqboard2000_ai_status,
				     DB2K_ACQ_STATUS_CONFIG_PIPE_FULL);
		if (ret)
			return ret;

		writew(DB2K_ACQ_CONTROL_ADC_PACER_ENABLE,
		       dev->mmio + DB2K_REG_ACQ_CONTROL);

		ret = comedi_timeout(dev, s, insn, daqboard2000_ai_status,
				     DB2K_ACQ_STATUS_LOGIC_SCANNING);
		if (ret)
			return ret;

		ret =
		comedi_timeout(dev, s, insn, daqboard2000_ai_status,
			       DB2K_ACQ_STATUS_RESULTS_FIFO_HAS_DATA);
		if (ret)
			return ret;

		data[i] = readw(dev->mmio + DB2K_REG_ACQ_RESULTS_FIFO);
		writew(DB2K_ACQ_CONTROL_ADC_PACER_DISABLE,
		       dev->mmio + DB2K_REG_ACQ_CONTROL);
		writew(DB2K_ACQ_CONTROL_SEQ_STOP_SCAN_LIST,
		       dev->mmio + DB2K_REG_ACQ_CONTROL);
	}

	return i;
}

static int daqboard2000_ao_eoc(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned long context)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int status;

	status = readw(dev->mmio + DB2K_REG_DAC_STATUS);
	if ((status & DB2K_DAC_STATUS_DAC_BUSY(chan)) == 0)
		return 0;
	return -EBUSY;
}

static int daqboard2000_ao_insn_write(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		unsigned int val = data[i];
		int ret;

		writew(val, dev->mmio + DB2K_REG_DAC_SETTING(chan));

		ret = comedi_timeout(dev, s, insn, daqboard2000_ao_eoc, 0);
		if (ret)
			return ret;

		s->readback[chan] = val;
	}

	return insn->n;
}

static void daqboard2000_reset_local_bus(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DB2K_SECR_LOCAL_BUS_HI, devpriv->plx + 0x6c);
	mdelay(10);
	writel(DB2K_SECR_LOCAL_BUS_LO, devpriv->plx + 0x6c);
	mdelay(10);
}

static void daqboard2000_reload_plx(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DB2K_SECR_RELOAD_LO, devpriv->plx + 0x6c);
	mdelay(10);
	writel(DB2K_SECR_RELOAD_HI, devpriv->plx + 0x6c);
	mdelay(10);
	writel(DB2K_SECR_RELOAD_LO, devpriv->plx + 0x6c);
	mdelay(10);
}

static void daqboard2000_pulse_prog_pin(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DB2K_SECR_PROG_PIN_HI, devpriv->plx + 0x6c);
	mdelay(10);
	writel(DB2K_SECR_PROG_PIN_LO, devpriv->plx + 0x6c);
	mdelay(10);	/* Not in the original code, but I like symmetry... */
}

static int daqboard2000_poll_cpld(struct comedi_device *dev, int mask)
{
	int result = 0;
	int i;
	int cpld;

	/* timeout after 50 tries -> 5ms */
	for (i = 0; i < 50; i++) {
		cpld = readw(dev->mmio + 0x1000);
		if ((cpld & mask) == mask) {
			result = 1;
			break;
		}
		usleep_range(100, 1000);
	}
	udelay(5);
	return result;
}

static int daqboard2000_write_cpld(struct comedi_device *dev, int data)
{
	int result = 0;

	usleep_range(10, 20);
	writew(data, dev->mmio + 0x1000);
	if ((readw(dev->mmio + 0x1000) & DAQBOARD2000_CPLD_INIT) ==
	    DAQBOARD2000_CPLD_INIT) {
		result = 1;
	}
	return result;
}

static int daqboard2000_load_firmware(struct comedi_device *dev,
				      const u8 *cpld_array, size_t len,
				      unsigned long context)
{
	struct daqboard2000_private *devpriv = dev->private;
	int result = -EIO;
	/* Read the serial EEPROM control register */
	int secr;
	int retry;
	size_t i;

	/* Check to make sure the serial eeprom is present on the board */
	secr = readl(devpriv->plx + 0x6c);
	if (!(secr & DAQBOARD2000_EEPROM_PRESENT))
		return -EIO;

	for (retry = 0; retry < 3; retry++) {
		daqboard2000_reset_local_bus(dev);
		daqboard2000_reload_plx(dev);
		daqboard2000_pulse_prog_pin(dev);
		if (daqboard2000_poll_cpld(dev, DAQBOARD2000_CPLD_INIT)) {
			for (i = 0; i < len; i++) {
				if (cpld_array[i] == 0xff &&
				    cpld_array[i + 1] == 0x20)
					break;
			}
			for (; i < len; i += 2) {
				int data =
				    (cpld_array[i] << 8) + cpld_array[i + 1];
				if (!daqboard2000_write_cpld(dev, data))
					break;
			}
			if (i >= len) {
				daqboard2000_reset_local_bus(dev);
				daqboard2000_reload_plx(dev);
				result = 0;
				break;
			}
		}
	}
	return result;
}

static void daqboard2000_adc_stop_dma_transfer(struct comedi_device *dev)
{
}

static void daqboard2000_adc_disarm(struct comedi_device *dev)
{
	/* Disable hardware triggers */
	udelay(2);
	writew(DB2K_TRIG_CONTROL_TYPE_ANALOG | DB2K_TRIG_CONTROL_DISABLE,
	       dev->mmio + DB2K_REG_TRIG_CONTROL);
	udelay(2);
	writew(DB2K_TRIG_CONTROL_TYPE_TTL | DB2K_TRIG_CONTROL_DISABLE,
	       dev->mmio + DB2K_REG_TRIG_CONTROL);

	/* Stop the scan list FIFO from loading the configuration pipe */
	udelay(2);
	writew(DB2K_ACQ_CONTROL_SEQ_STOP_SCAN_LIST,
	       dev->mmio + DB2K_REG_ACQ_CONTROL);

	/* Stop the pacer clock */
	udelay(2);
	writew(DB2K_ACQ_CONTROL_ADC_PACER_DISABLE,
	       dev->mmio + DB2K_REG_ACQ_CONTROL);

	/* Stop the input dma (abort channel 1) */
	daqboard2000_adc_stop_dma_transfer(dev);
}

static void daqboard2000_activate_reference_dacs(struct comedi_device *dev)
{
	unsigned int val;
	int timeout;

	/*  Set the + reference dac value in the FPGA */
	writew(DB2K_REF_DACS_SET | DB2K_REF_DACS_SELECT_POS_REF,
	       dev->mmio + DB2K_REG_REF_DACS);
	for (timeout = 0; timeout < 20; timeout++) {
		val = readw(dev->mmio + DB2K_REG_DAC_STATUS);
		if ((val & DB2K_DAC_STATUS_REF_BUSY) == 0)
			break;
		udelay(2);
	}

	/*  Set the - reference dac value in the FPGA */
	writew(DB2K_REF_DACS_SET | DB2K_REF_DACS_SELECT_NEG_REF,
	       dev->mmio + DB2K_REG_REF_DACS);
	for (timeout = 0; timeout < 20; timeout++) {
		val = readw(dev->mmio + DB2K_REG_DAC_STATUS);
		if ((val & DB2K_DAC_STATUS_REF_BUSY) == 0)
			break;
		udelay(2);
	}
}

static void daqboard2000_initialize_ctrs(struct comedi_device *dev)
{
}

static void daqboard2000_initialize_tmrs(struct comedi_device *dev)
{
}

static void daqboard2000_dac_disarm(struct comedi_device *dev)
{
}

static void daqboard2000_initialize_adc(struct comedi_device *dev)
{
	daqboard2000_adc_disarm(dev);
	daqboard2000_activate_reference_dacs(dev);
	daqboard2000_initialize_ctrs(dev);
	daqboard2000_initialize_tmrs(dev);
}

static void daqboard2000_initialize_dac(struct comedi_device *dev)
{
	daqboard2000_dac_disarm(dev);
}

static int daqboard2000_8255_cb(struct comedi_device *dev,
				int dir, int port, int data,
				unsigned long iobase)
{
	if (dir) {
		writew(data, dev->mmio + iobase + port * 2);
		return 0;
	}
	return readw(dev->mmio + iobase + port * 2);
}

static const void *daqboard2000_find_boardinfo(struct comedi_device *dev,
					       struct pci_dev *pcidev)
{
	const struct daq200_boardtype *board;
	int i;

	if (pcidev->subsystem_device != PCI_VENDOR_ID_IOTECH)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(boardtypes); i++) {
		board = &boardtypes[i];
		if (pcidev->subsystem_device == board->id)
			return board;
	}
	return NULL;
}

static int daqboard2000_auto_attach(struct comedi_device *dev,
				    unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct daq200_boardtype *board;
	struct daqboard2000_private *devpriv;
	struct comedi_subdevice *s;
	int result;

	board = daqboard2000_find_boardinfo(dev, pcidev);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	result = comedi_pci_enable(dev);
	if (result)
		return result;

	devpriv->plx = pci_ioremap_bar(pcidev, 0);
	dev->mmio = pci_ioremap_bar(pcidev, 2);
	if (!devpriv->plx || !dev->mmio)
		return -ENOMEM;

	result = comedi_alloc_subdevices(dev, 3);
	if (result)
		return result;

	readl(devpriv->plx + 0x6c);

	result = comedi_load_firmware(dev, &comedi_to_pci_dev(dev)->dev,
				      DAQBOARD2000_FIRMWARE,
				      daqboard2000_load_firmware, 0);
	if (result < 0)
		return result;

	daqboard2000_initialize_adc(dev);
	daqboard2000_initialize_dac(dev);

	s = &dev->subdevices[0];
	/* ai subdevice */
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 24;
	s->maxdata = 0xffff;
	s->insn_read = daqboard2000_ai_insn_read;
	s->range_table = &range_daqboard2000_ai;

	s = &dev->subdevices[1];
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 2;
	s->maxdata = 0xffff;
	s->insn_write = daqboard2000_ao_insn_write;
	s->range_table = &range_bipolar10;

	result = comedi_alloc_subdev_readback(s);
	if (result)
		return result;

	s = &dev->subdevices[2];
	return subdev_8255_init(dev, s, daqboard2000_8255_cb,
				DB2K_REG_DIO_P2_EXP_IO_8_BIT);
}

static void daqboard2000_detach(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	if (devpriv && devpriv->plx)
		iounmap(devpriv->plx);
	comedi_pci_detach(dev);
}

static struct comedi_driver daqboard2000_driver = {
	.driver_name	= "daqboard2000",
	.module		= THIS_MODULE,
	.auto_attach	= daqboard2000_auto_attach,
	.detach		= daqboard2000_detach,
};

static int daqboard2000_pci_probe(struct pci_dev *dev,
				  const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &daqboard2000_driver,
				      id->driver_data);
}

static const struct pci_device_id daqboard2000_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IOTECH, 0x0409) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, daqboard2000_pci_table);

static struct pci_driver daqboard2000_pci_driver = {
	.name		= "daqboard2000",
	.id_table	= daqboard2000_pci_table,
	.probe		= daqboard2000_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(daqboard2000_driver, daqboard2000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(DAQBOARD2000_FIRMWARE);
