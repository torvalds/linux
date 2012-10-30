/*
   comedi/drivers/daqboard2000.c
   hardware driver for IOtech DAQboard/2000

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>

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
Driver: daqboard2000
Description: IOTech DAQBoard/2000
Author: Anders Blomdell <anders.blomdell@control.lth.se>
Status: works
Updated: Mon, 14 Apr 2008 15:28:52 +0100
Devices: [IOTech] DAQBoard/2000 (daqboard2000)

Much of the functionality of this driver was determined from reading
the source code for the Windows driver.

The FPGA on the board requires fimware, which is available from
http://www.comedi.org in the comedi_nonfree_firmware tarball.

Configuration options: not applicable, uses PCI auto config
*/
/*
   This card was obviously never intended to leave the Windows world,
   since it lacked all kind of hardware documentation (except for cable
   pinouts, plug and pray has something to catch up with yet).

   With some help from our swedish distributor, we got the Windows sourcecode
   for the card, and here are the findings so far.

   1. A good document that describes the PCI interface chip is 9080db-106.pdf
      available from http://www.plxtech.com/products/io/pci9080 

   2. The initialization done so far is:
        a. program the FPGA (windows code sans a lot of error messages)
	b.

   3. Analog out seems to work OK with DAC's disabled, if DAC's are enabled,
      you have to output values to all enabled DAC's until result appears, I
      guess that it has something to do with pacer clocks, but the source
      gives me no clues. I'll keep it simple so far.

   4. Analog in.
        Each channel in the scanlist seems to be controlled by four
	control words:

        Word0:
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          ! | | | ! | | | ! | | | ! | | | !
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

        Word1:
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          ! | | | ! | | | ! | | | ! | | | !
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	   |             |       | | | | |
           +------+------+       | | | | +-- Digital input (??)
		  |		 | | | +---- 10 us settling time
		  |		 | | +------ Suspend acquisition (last to scan)
		  |		 | +-------- Simultaneous sample and hold
		  |		 +---------- Signed data format
		  +------------------------- Correction offset low

        Word2:
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          ! | | | ! | | | ! | | | ! | | | !
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           |     | |     | | | | | |     |
           +-----+ +--+--+ +++ +++ +--+--+
              |       |     |   |     +----- Expansion channel
	      |       |     |   +----------- Expansion gain
              |       |     +--------------- Channel (low)
	      |       +--------------------- Correction offset high
	      +----------------------------- Correction gain low
        Word3:
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
          ! | | | ! | | | ! | | | ! | | | !
          +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
           |             | | | |   | | | |
           +------+------+ | | +-+-+ | | +-- Low bank enable
                  |        | |   |   | +---- High bank enable
                  |        | |   |   +------ Hi/low select
		  |    	   | |   +---------- Gain (1,?,2,4,8,16,32,64)
		  |    	   | +-------------- differential/single ended
		  |    	   +---------------- Unipolar
		  +------------------------- Correction gain high

   999. The card seems to have an incredible amount of capabilities, but
        trying to reverse engineer them from the Windows source is beyond my
	patience.

 */

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>

#include "8255.h"

#define DAQBOARD2000_FIRMWARE		"daqboard2000_firmware.bin"

#define DAQBOARD2000_SUBSYSTEM_IDS2 	0x0002	/* Daqboard/2000 - 2 Dacs */
#define DAQBOARD2000_SUBSYSTEM_IDS4 	0x0004	/* Daqboard/2000 - 4 Dacs */

/* Initialization bits for the Serial EEPROM Control Register */
#define DAQBOARD2000_SECRProgPinHi      0x8001767e
#define DAQBOARD2000_SECRProgPinLo      0x8000767e
#define DAQBOARD2000_SECRLocalBusHi     0xc000767e
#define DAQBOARD2000_SECRLocalBusLo     0x8000767e
#define DAQBOARD2000_SECRReloadHi       0xa000767e
#define DAQBOARD2000_SECRReloadLo       0x8000767e

/* SECR status bits */
#define DAQBOARD2000_EEPROM_PRESENT     0x10000000

/* CPLD status bits */
#define DAQBOARD2000_CPLD_INIT 		0x0002
#define DAQBOARD2000_CPLD_DONE 		0x0004

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
#define acqControl			0x00		/* u16 */
#define acqScanListFIFO			0x02		/* u16 */
#define acqPacerClockDivLow		0x04		/* u32 */
#define acqScanCounter			0x08		/* u16 */
#define acqPacerClockDivHigh		0x0a		/* u16 */
#define acqTriggerCount			0x0c		/* u16 */
#define acqResultsFIFO			0x10		/* u16 */
#define acqResultsShadow		0x14		/* u16 */
#define acqAdcResult			0x18		/* u16 */
#define dacScanCounter			0x1c		/* u16 */
#define dacControl			0x20		/* u16 */
#define dacFIFO				0x24		/* s16 */
#define dacPacerClockDiv		0x2a		/* u16 */
#define refDacs				0x2c		/* u16 */
#define dioControl			0x30		/* u16 */
#define dioP3hsioData			0x32		/* s16 */
#define dioP3Control			0x34		/* u16 */
#define calEepromControl		0x36		/* u16 */
#define dacSetting(x)			(0x38 + (x)*2)	/* s16 */
#define dioP2ExpansionIO8Bit		0x40		/* s16 */
#define ctrTmrControl			0x80		/* u16 */
#define ctrInput(x)			(0x88 + (x)*2)	/* s16 */
#define timerDivisor(x)			(0xa0 + (x)*2)	/* u16 */
#define dmaControl			0xb0		/* u16 */
#define trigControl			0xb2		/* u16 */
#define calEeprom			0xb8		/* u16 */
#define acqDigitalMark			0xba		/* u16 */
#define trigDacs			0xbc		/* u16 */
#define dioP2ExpansionIO16Bit(x)	(0xc0 + (x)*2)	/* s16 */

/* Scan Sequencer programming */
#define DAQBOARD2000_SeqStartScanList            0x0011
#define DAQBOARD2000_SeqStopScanList             0x0010

/* Prepare for acquisition */
#define DAQBOARD2000_AcqResetScanListFifo        0x0004
#define DAQBOARD2000_AcqResetResultsFifo         0x0002
#define DAQBOARD2000_AcqResetConfigPipe          0x0001

/* Acqusition status bits */
#define DAQBOARD2000_AcqResultsFIFOMore1Sample   0x0001
#define DAQBOARD2000_AcqResultsFIFOHasValidData  0x0002
#define DAQBOARD2000_AcqResultsFIFOOverrun       0x0004
#define DAQBOARD2000_AcqLogicScanning            0x0008
#define DAQBOARD2000_AcqConfigPipeFull           0x0010
#define DAQBOARD2000_AcqScanListFIFOEmpty        0x0020
#define DAQBOARD2000_AcqAdcNotReady              0x0040
#define DAQBOARD2000_ArbitrationFailure          0x0080
#define DAQBOARD2000_AcqPacerOverrun             0x0100
#define DAQBOARD2000_DacPacerOverrun             0x0200
#define DAQBOARD2000_AcqHardwareError            0x01c0

/* Scan Sequencer programming */
#define DAQBOARD2000_SeqStartScanList            0x0011
#define DAQBOARD2000_SeqStopScanList             0x0010

/* Pacer Clock Control */
#define DAQBOARD2000_AdcPacerInternal            0x0030
#define DAQBOARD2000_AdcPacerExternal            0x0032
#define DAQBOARD2000_AdcPacerEnable              0x0031
#define DAQBOARD2000_AdcPacerEnableDacPacer      0x0034
#define DAQBOARD2000_AdcPacerDisable             0x0030
#define DAQBOARD2000_AdcPacerNormalMode          0x0060
#define DAQBOARD2000_AdcPacerCompatibilityMode   0x0061
#define DAQBOARD2000_AdcPacerInternalOutEnable   0x0008
#define DAQBOARD2000_AdcPacerExternalRising      0x0100

/* DAC status */
#define DAQBOARD2000_DacFull                     0x0001
#define DAQBOARD2000_RefBusy                     0x0002
#define DAQBOARD2000_TrgBusy                     0x0004
#define DAQBOARD2000_CalBusy                     0x0008
#define DAQBOARD2000_Dac0Busy                    0x0010
#define DAQBOARD2000_Dac1Busy                    0x0020
#define DAQBOARD2000_Dac2Busy                    0x0040
#define DAQBOARD2000_Dac3Busy                    0x0080

/* DAC control */
#define DAQBOARD2000_Dac0Enable                  0x0021
#define DAQBOARD2000_Dac1Enable                  0x0031
#define DAQBOARD2000_Dac2Enable                  0x0041
#define DAQBOARD2000_Dac3Enable                  0x0051
#define DAQBOARD2000_DacEnableBit                0x0001
#define DAQBOARD2000_Dac0Disable                 0x0020
#define DAQBOARD2000_Dac1Disable                 0x0030
#define DAQBOARD2000_Dac2Disable                 0x0040
#define DAQBOARD2000_Dac3Disable                 0x0050
#define DAQBOARD2000_DacResetFifo                0x0004
#define DAQBOARD2000_DacPatternDisable           0x0060
#define DAQBOARD2000_DacPatternEnable            0x0061
#define DAQBOARD2000_DacSelectSignedData         0x0002
#define DAQBOARD2000_DacSelectUnsignedData       0x0000

/* Trigger Control */
#define DAQBOARD2000_TrigAnalog                  0x0000
#define DAQBOARD2000_TrigTTL                     0x0010
#define DAQBOARD2000_TrigTransHiLo               0x0004
#define DAQBOARD2000_TrigTransLoHi               0x0000
#define DAQBOARD2000_TrigAbove                   0x0000
#define DAQBOARD2000_TrigBelow                   0x0004
#define DAQBOARD2000_TrigLevelSense              0x0002
#define DAQBOARD2000_TrigEdgeSense               0x0000
#define DAQBOARD2000_TrigEnable                  0x0001
#define DAQBOARD2000_TrigDisable                 0x0000

/* Reference Dac Selection */
#define DAQBOARD2000_PosRefDacSelect             0x0100
#define DAQBOARD2000_NegRefDacSelect             0x0000

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
	void __iomem *daq;
	void __iomem *plx;
	unsigned int ao_readback[2];
};

static void writeAcqScanListEntry(struct comedi_device *dev, u16 entry)
{
	struct daqboard2000_private *devpriv = dev->private;

	/* udelay(4); */
	writew(entry & 0x00ff, devpriv->daq + acqScanListFIFO);
	/* udelay(4); */
	writew((entry >> 8) & 0x00ff, devpriv->daq + acqScanListFIFO);
}

static void setup_sampling(struct comedi_device *dev, int chan, int gain)
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
/*
  dev->eeprom.correctionDACSE[i][j][k].offset = 0x800;
  dev->eeprom.correctionDACSE[i][j][k].gain = 0xc00;
*/
	/* These should be read from EEPROM */
	word2 |= 0x0800;
	word3 |= 0xc000;
	writeAcqScanListEntry(dev, word0);
	writeAcqScanListEntry(dev, word1);
	writeAcqScanListEntry(dev, word2);
	writeAcqScanListEntry(dev, word3);
}

static int daqboard2000_ai_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct daqboard2000_private *devpriv = dev->private;
	unsigned int val;
	int gain, chan, timeout;
	int i;

	writew(DAQBOARD2000_AcqResetScanListFifo |
	       DAQBOARD2000_AcqResetResultsFifo |
	       DAQBOARD2000_AcqResetConfigPipe, devpriv->daq + acqControl);

	/*
	 * If pacer clock is not set to some high value (> 10 us), we
	 * risk multiple samples to be put into the result FIFO.
	 */
	/* 1 second, should be long enough */
	writel(1000000, devpriv->daq + acqPacerClockDivLow);
	writew(0, devpriv->daq + acqPacerClockDivHigh);

	gain = CR_RANGE(insn->chanspec);
	chan = CR_CHAN(insn->chanspec);

	/* This doesn't look efficient.  I decided to take the conservative
	 * approach when I did the insn conversion.  Perhaps it would be
	 * better to have broken it completely, then someone would have been
	 * forced to fix it.  --ds */
	for (i = 0; i < insn->n; i++) {
		setup_sampling(dev, chan, gain);
		/* Enable reading from the scanlist FIFO */
		writew(DAQBOARD2000_SeqStartScanList,
		       devpriv->daq + acqControl);
		for (timeout = 0; timeout < 20; timeout++) {
			val = readw(devpriv->daq + acqControl);
			if (val & DAQBOARD2000_AcqConfigPipeFull)
				break;
			/* udelay(2); */
		}
		writew(DAQBOARD2000_AdcPacerEnable, devpriv->daq + acqControl);
		for (timeout = 0; timeout < 20; timeout++) {
			val = readw(devpriv->daq + acqControl);
			if (val & DAQBOARD2000_AcqLogicScanning)
				break;
			/* udelay(2); */
		}
		for (timeout = 0; timeout < 20; timeout++) {
			val = readw(devpriv->daq + acqControl);
			if (val & DAQBOARD2000_AcqResultsFIFOHasValidData)
				break;
			/* udelay(2); */
		}
		data[i] = readw(devpriv->daq + acqResultsFIFO);
		writew(DAQBOARD2000_AdcPacerDisable, devpriv->daq + acqControl);
		writew(DAQBOARD2000_SeqStopScanList, devpriv->daq + acqControl);
	}

	return i;
}

static int daqboard2000_ao_insn_read(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	struct daqboard2000_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int daqboard2000_ao_insn_write(struct comedi_device *dev,
				      struct comedi_subdevice *s,
				      struct comedi_insn *insn,
				      unsigned int *data)
{
	struct daqboard2000_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned int val;
	int timeout;
	int i;

	for (i = 0; i < insn->n; i++) {
#if 0
		/*
		 * OK, since it works OK without enabling the DAC's,
		 * let's keep it as simple as possible...
		 */
		writew((chan + 2) * 0x0010 | 0x0001,
		       devpriv->daq + dacControl);
		udelay(1000);
#endif
		writew(data[i], devpriv->daq + dacSetting(chan));
		for (timeout = 0; timeout < 20; timeout++) {
			val = readw(devpriv->daq + dacControl);
			if ((val & ((chan + 1) * 0x0010)) == 0)
				break;
			/* udelay(2); */
		}
		devpriv->ao_readback[chan] = data[i];
#if 0
		/*
		 * Since we never enabled the DAC's, we don't need
		 * to disable it...
		 */
		writew((chan + 2) * 0x0010 | 0x0000,
		       devpriv->daq + dacControl);
		udelay(1000);
#endif
	}

	return i;
}

static void daqboard2000_resetLocalBus(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DAQBOARD2000_SECRLocalBusHi, devpriv->plx + 0x6c);
	udelay(10000);
	writel(DAQBOARD2000_SECRLocalBusLo, devpriv->plx + 0x6c);
	udelay(10000);
}

static void daqboard2000_reloadPLX(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DAQBOARD2000_SECRReloadLo, devpriv->plx + 0x6c);
	udelay(10000);
	writel(DAQBOARD2000_SECRReloadHi, devpriv->plx + 0x6c);
	udelay(10000);
	writel(DAQBOARD2000_SECRReloadLo, devpriv->plx + 0x6c);
	udelay(10000);
}

static void daqboard2000_pulseProgPin(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	writel(DAQBOARD2000_SECRProgPinHi, devpriv->plx + 0x6c);
	udelay(10000);
	writel(DAQBOARD2000_SECRProgPinLo, devpriv->plx + 0x6c);
	udelay(10000);		/* Not in the original code, but I like symmetry... */
}

static int daqboard2000_pollCPLD(struct comedi_device *dev, int mask)
{
	struct daqboard2000_private *devpriv = dev->private;
	int result = 0;
	int i;
	int cpld;

	/* timeout after 50 tries -> 5ms */
	for (i = 0; i < 50; i++) {
		cpld = readw(devpriv->daq + 0x1000);
		if ((cpld & mask) == mask) {
			result = 1;
			break;
		}
		udelay(100);
	}
	udelay(5);
	return result;
}

static int daqboard2000_writeCPLD(struct comedi_device *dev, int data)
{
	struct daqboard2000_private *devpriv = dev->private;
	int result = 0;

	udelay(10);
	writew(data, devpriv->daq + 0x1000);
	if ((readw(devpriv->daq + 0x1000) & DAQBOARD2000_CPLD_INIT) ==
	    DAQBOARD2000_CPLD_INIT) {
		result = 1;
	}
	return result;
}

static int initialize_daqboard2000(struct comedi_device *dev,
				   const u8 *cpld_array, size_t len)
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
		daqboard2000_resetLocalBus(dev);
		daqboard2000_reloadPLX(dev);
		daqboard2000_pulseProgPin(dev);
		if (daqboard2000_pollCPLD(dev, DAQBOARD2000_CPLD_INIT)) {
			for (i = 0; i < len; i++) {
				if (cpld_array[i] == 0xff &&
				    cpld_array[i + 1] == 0x20)
					break;
			}
			for (; i < len; i += 2) {
				int data =
				    (cpld_array[i] << 8) + cpld_array[i + 1];
				if (!daqboard2000_writeCPLD(dev, data))
					break;
			}
			if (i >= len) {
				daqboard2000_resetLocalBus(dev);
				daqboard2000_reloadPLX(dev);
				result = 0;
				break;
			}
		}
	}
	return result;
}

static int daqboard2000_upload_firmware(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct firmware *fw;
	int ret;

	ret = request_firmware(&fw, DAQBOARD2000_FIRMWARE, &pcidev->dev);
	if (ret)
		return ret;

	ret = initialize_daqboard2000(dev, fw->data, fw->size);
	release_firmware(fw);

	return ret;
}

static void daqboard2000_adcStopDmaTransfer(struct comedi_device *dev)
{
}

static void daqboard2000_adcDisarm(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;

	/* Disable hardware triggers */
	udelay(2);
	writew(DAQBOARD2000_TrigAnalog | DAQBOARD2000_TrigDisable,
	       devpriv->daq + trigControl);
	udelay(2);
	writew(DAQBOARD2000_TrigTTL | DAQBOARD2000_TrigDisable,
	       devpriv->daq + trigControl);

	/* Stop the scan list FIFO from loading the configuration pipe */
	udelay(2);
	writew(DAQBOARD2000_SeqStopScanList, devpriv->daq + acqControl);

	/* Stop the pacer clock */
	udelay(2);
	writew(DAQBOARD2000_AdcPacerDisable, devpriv->daq + acqControl);

	/* Stop the input dma (abort channel 1) */
	daqboard2000_adcStopDmaTransfer(dev);
}

static void daqboard2000_activateReferenceDacs(struct comedi_device *dev)
{
	struct daqboard2000_private *devpriv = dev->private;
	unsigned int val;
	int timeout;

	/*  Set the + reference dac value in the FPGA */
	writew(0x80 | DAQBOARD2000_PosRefDacSelect, devpriv->daq + refDacs);
	for (timeout = 0; timeout < 20; timeout++) {
		val = readw(devpriv->daq + dacControl);
		if ((val & DAQBOARD2000_RefBusy) == 0)
			break;
		udelay(2);
	}

	/*  Set the - reference dac value in the FPGA */
	writew(0x80 | DAQBOARD2000_NegRefDacSelect, devpriv->daq + refDacs);
	for (timeout = 0; timeout < 20; timeout++) {
		val = readw(devpriv->daq + dacControl);
		if ((val & DAQBOARD2000_RefBusy) == 0)
			break;
		udelay(2);
	}
}

static void daqboard2000_initializeCtrs(struct comedi_device *dev)
{
}

static void daqboard2000_initializeTmrs(struct comedi_device *dev)
{
}

static void daqboard2000_dacDisarm(struct comedi_device *dev)
{
}

static void daqboard2000_initializeAdc(struct comedi_device *dev)
{
	daqboard2000_adcDisarm(dev);
	daqboard2000_activateReferenceDacs(dev);
	daqboard2000_initializeCtrs(dev);
	daqboard2000_initializeTmrs(dev);
}

static void daqboard2000_initializeDac(struct comedi_device *dev)
{
	daqboard2000_dacDisarm(dev);
}

static int daqboard2000_8255_cb(int dir, int port, int data,
				unsigned long ioaddr)
{
	void __iomem *mmio_base = (void __iomem *)ioaddr;

	if (dir) {
		writew(data, mmio_base + port * 2);
		return 0;
	} else {
		return readw(mmio_base + port * 2);
	}
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

static int __devinit daqboard2000_auto_attach(struct comedi_device *dev,
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

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	result = comedi_pci_enable(pcidev, dev->driver->driver_name);
	if (result < 0)
		return result;
	dev->iobase = 1;	/* the "detach" needs this */

	devpriv->plx = ioremap(pci_resource_start(pcidev, 0),
			       pci_resource_len(pcidev, 0));
	devpriv->daq = ioremap(pci_resource_start(pcidev, 2),
			       pci_resource_len(pcidev, 2));
	if (!devpriv->plx || !devpriv->daq)
		return -ENOMEM;

	result = comedi_alloc_subdevices(dev, 3);
	if (result)
		return result;

	readl(devpriv->plx + 0x6c);

	result = daqboard2000_upload_firmware(dev);
	if (result < 0)
		return result;

	daqboard2000_initializeAdc(dev);
	daqboard2000_initializeDac(dev);

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
	s->insn_read = daqboard2000_ao_insn_read;
	s->insn_write = daqboard2000_ao_insn_write;
	s->range_table = &range_bipolar10;

	s = &dev->subdevices[2];
	result = subdev_8255_init(dev, s, daqboard2000_8255_cb,
			(unsigned long)(devpriv->daq + dioP2ExpansionIO8Bit));
	if (result)
		return result;

	dev_info(dev->class_dev, "%s: %s attached\n",
		dev->driver->driver_name, dev->board_name);

	return 0;
}

static void daqboard2000_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct daqboard2000_private *devpriv = dev->private;

	if (dev->subdevices)
		subdev_8255_cleanup(dev, &dev->subdevices[2]);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		if (devpriv->daq)
			iounmap(devpriv->daq);
		if (devpriv->plx)
			iounmap(devpriv->plx);
	}
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
		pci_dev_put(pcidev);
	}
}

static struct comedi_driver daqboard2000_driver = {
	.driver_name	= "daqboard2000",
	.module		= THIS_MODULE,
	.auto_attach	= daqboard2000_auto_attach,
	.detach		= daqboard2000_detach,
};

static int __devinit daqboard2000_pci_probe(struct pci_dev *dev,
					    const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &daqboard2000_driver);
}

static void __devexit daqboard2000_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(daqboard2000_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_IOTECH, 0x0409) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, daqboard2000_pci_table);

static struct pci_driver daqboard2000_pci_driver = {
	.name		= "daqboard2000",
	.id_table	= daqboard2000_pci_table,
	.probe		= daqboard2000_pci_probe,
	.remove		= __devexit_p(daqboard2000_pci_remove),
};
module_comedi_pci_driver(daqboard2000_driver, daqboard2000_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(DAQBOARD2000_FIRMWARE);
