/*
    comedi/drivers/icp_multi.c

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-2002 David A. Schleef <ds@schleef.org>

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
Driver: icp_multi
Description: Inova ICP_MULTI
Author: Anne Smorthit <anne.smorthit@sfwte.ch>
Devices: [Inova] ICP_MULTI (icp_multi)
Status: works

The driver works for analog input and output and digital input and output.
It does not work with interrupts or with the counters.  Currently no support
for DMA.

It has 16 single-ended or 8 differential Analogue Input channels with 12-bit
resolution.  Ranges : 5V, 10V, +/-5V, +/-10V, 0..20mA and 4..20mA.  Input
ranges can be individually programmed for each channel.  Voltage or current
measurement is selected by jumper.

There are 4 x 12-bit Analogue Outputs.  Ranges : 5V, 10V, +/-5V, +/-10V

16 x Digital Inputs, 24V

8 x Digital Outputs, 24V, 1A

4 x 16-bit counters

Configuration options: not applicable, uses PCI auto config
*/

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#define PCI_DEVICE_ID_ICP_MULTI	0x8000

#define ICP_MULTI_ADC_CSR	0	/* R/W: ADC command/status register */
#define ICP_MULTI_AI		2	/* R:   Analogue input data */
#define ICP_MULTI_DAC_CSR	4	/* R/W: DAC command/status register */
#define ICP_MULTI_AO		6	/* R/W: Analogue output data */
#define ICP_MULTI_DI		8	/* R/W: Digital inouts */
#define ICP_MULTI_DO		0x0A	/* R/W: Digital outputs */
#define ICP_MULTI_INT_EN	0x0C	/* R/W: Interrupt enable register */
#define ICP_MULTI_INT_STAT	0x0E	/* R/W: Interrupt status register */
#define ICP_MULTI_CNTR0		0x10	/* R/W: Counter 0 */
#define ICP_MULTI_CNTR1		0x12	/* R/W: counter 1 */
#define ICP_MULTI_CNTR2		0x14	/* R/W: Counter 2 */
#define ICP_MULTI_CNTR3		0x16	/* R/W: Counter 3 */

#define ICP_MULTI_SIZE		0x20	/* 32 bytes */

/*  Define bits from ADC command/status register */
#define	ADC_ST		0x0001	/* Start ADC */
#define	ADC_BSY		0x0001	/* ADC busy */
#define ADC_BI		0x0010	/* Bipolar input range 1 = bipolar */
#define ADC_RA		0x0020	/* Input range 0 = 5V, 1 = 10V */
#define	ADC_DI		0x0040	/* Differential input mode 1 = differential */

/*  Define bits from DAC command/status register */
#define	DAC_ST		0x0001	/* Start DAC */
#define DAC_BSY		0x0001	/* DAC busy */
#define	DAC_BI		0x0010	/* Bipolar input range 1 = bipolar */
#define	DAC_RA		0x0020	/* Input range 0 = 5V, 1 = 10V */

/*  Define bits from interrupt enable/status registers */
#define	ADC_READY	0x0001	/* A/d conversion ready interrupt */
#define	DAC_READY	0x0002	/* D/a conversion ready interrupt */
#define	DOUT_ERROR	0x0004	/* Digital output error interrupt */
#define	DIN_STATUS	0x0008	/* Digital input status change interrupt */
#define	CIE0		0x0010	/* Counter 0 overrun interrupt */
#define	CIE1		0x0020	/* Counter 1 overrun interrupt */
#define	CIE2		0x0040	/* Counter 2 overrun interrupt */
#define	CIE3		0x0080	/* Counter 3 overrun interrupt */

/*  Useful definitions */
#define	Status_IRQ	0x00ff	/*  All interrupts */

/*  Define analogue range */
static const struct comedi_lrange range_analog = { 4, {
						       UNI_RANGE(5),
						       UNI_RANGE(10),
						       BIP_RANGE(5),
						       BIP_RANGE(10)
						       }
};

static const char range_codes_analog[] = { 0x00, 0x20, 0x10, 0x30 };

/*
==============================================================================
	Data & Structure declarations
==============================================================================
*/

struct icp_multi_private {
	char valid;		/*  card is usable */
	void __iomem *io_addr;		/*  Pointer to mapped io address */
	unsigned int AdcCmdStatus;	/*  ADC Command/Status register */
	unsigned int DacCmdStatus;	/*  DAC Command/Status register */
	unsigned int IntEnable;	/*  Interrupt Enable register */
	unsigned int IntStatus;	/*  Interrupt Status register */
	unsigned int act_chanlist[32];	/*  list of scaned channel */
	unsigned char act_chanlist_len;	/*  len of scanlist */
	unsigned char act_chanlist_pos;	/*  actual position in MUX list */
	unsigned int *ai_chanlist;	/*  actaul chanlist */
	short *ai_data;		/*  data buffer */
	short ao_data[4];	/*  data output buffer */
	short di_data;		/*  Digital input data */
	unsigned int do_data;	/*  Remember digital output data */
};

static void setup_channel_list(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int *chanlist, unsigned int n_chan)
{
	struct icp_multi_private *devpriv = dev->private;
	unsigned int i, range, chanprog;
	unsigned int diff;

	devpriv->act_chanlist_len = n_chan;
	devpriv->act_chanlist_pos = 0;

	for (i = 0; i < n_chan; i++) {
		/*  Get channel */
		chanprog = CR_CHAN(chanlist[i]);

		/*  Determine if it is a differential channel (Bit 15  = 1) */
		if (CR_AREF(chanlist[i]) == AREF_DIFF) {
			diff = 1;
			chanprog &= 0x0007;
		} else {
			diff = 0;
			chanprog &= 0x000f;
		}

		/*  Clear channel, range and input mode bits
		 *  in A/D command/status register */
		devpriv->AdcCmdStatus &= 0xf00f;

		/*  Set channel number and differential mode status bit */
		if (diff) {
			/*  Set channel number, bits 9-11 & mode, bit 6 */
			devpriv->AdcCmdStatus |= (chanprog << 9);
			devpriv->AdcCmdStatus |= ADC_DI;
		} else
			/*  Set channel number, bits 8-11 */
			devpriv->AdcCmdStatus |= (chanprog << 8);

		/*  Get range for current channel */
		range = range_codes_analog[CR_RANGE(chanlist[i])];
		/*  Set range. bits 4-5 */
		devpriv->AdcCmdStatus |= range;

		/* Output channel, range, mode to ICP Multi */
		writew(devpriv->AdcCmdStatus,
		       devpriv->io_addr + ICP_MULTI_ADC_CSR);
	}
}

static int icp_multi_insn_read_ai(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct icp_multi_private *devpriv = dev->private;
	int n, timeout;

	/*  Disable A/D conversion ready interrupt */
	devpriv->IntEnable &= ~ADC_READY;
	writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

	/*  Clear interrupt status */
	devpriv->IntStatus |= ADC_READY;
	writew(devpriv->IntStatus, devpriv->io_addr + ICP_MULTI_INT_STAT);

	/*  Set up appropriate channel, mode and range data, for specified ch */
	setup_channel_list(dev, s, &insn->chanspec, 1);

	for (n = 0; n < insn->n; n++) {
		/*  Set start ADC bit */
		devpriv->AdcCmdStatus |= ADC_ST;
		writew(devpriv->AdcCmdStatus,
		       devpriv->io_addr + ICP_MULTI_ADC_CSR);
		devpriv->AdcCmdStatus &= ~ADC_ST;

		udelay(1);

		/*  Wait for conversion to complete, or get fed up waiting */
		timeout = 100;
		while (timeout--) {
			if (!(readw(devpriv->io_addr +
				    ICP_MULTI_ADC_CSR) & ADC_BSY))
				goto conv_finish;

			udelay(1);
		}

		/*  If we reach here, a timeout has occurred */
		comedi_error(dev, "A/D insn timeout");

		/*  Disable interrupt */
		devpriv->IntEnable &= ~ADC_READY;
		writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

		/*  Clear interrupt status */
		devpriv->IntStatus |= ADC_READY;
		writew(devpriv->IntStatus,
		       devpriv->io_addr + ICP_MULTI_INT_STAT);

		/*  Clear data received */
		data[n] = 0;

		return -ETIME;

conv_finish:
		data[n] =
		    (readw(devpriv->io_addr + ICP_MULTI_AI) >> 4) & 0x0fff;
	}

	/*  Disable interrupt */
	devpriv->IntEnable &= ~ADC_READY;
	writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

	/*  Clear interrupt status */
	devpriv->IntStatus |= ADC_READY;
	writew(devpriv->IntStatus, devpriv->io_addr + ICP_MULTI_INT_STAT);

	return n;
}

static int icp_multi_insn_write_ao(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct icp_multi_private *devpriv = dev->private;
	int n, chan, range, timeout;

	/*  Disable D/A conversion ready interrupt */
	devpriv->IntEnable &= ~DAC_READY;
	writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

	/*  Clear interrupt status */
	devpriv->IntStatus |= DAC_READY;
	writew(devpriv->IntStatus, devpriv->io_addr + ICP_MULTI_INT_STAT);

	/*  Get channel number and range */
	chan = CR_CHAN(insn->chanspec);
	range = CR_RANGE(insn->chanspec);

	/*  Set up range and channel data */
	/*  Bit 4 = 1 : Bipolar */
	/*  Bit 5 = 0 : 5V */
	/*  Bit 5 = 1 : 10V */
	/*  Bits 8-9 : Channel number */
	devpriv->DacCmdStatus &= 0xfccf;
	devpriv->DacCmdStatus |= range_codes_analog[range];
	devpriv->DacCmdStatus |= (chan << 8);

	writew(devpriv->DacCmdStatus, devpriv->io_addr + ICP_MULTI_DAC_CSR);

	for (n = 0; n < insn->n; n++) {
		/*  Wait for analogue output data register to be
		 *  ready for new data, or get fed up waiting */
		timeout = 100;
		while (timeout--) {
			if (!(readw(devpriv->io_addr +
				    ICP_MULTI_DAC_CSR) & DAC_BSY))
				goto dac_ready;

			udelay(1);
		}

		/*  If we reach here, a timeout has occurred */
		comedi_error(dev, "D/A insn timeout");

		/*  Disable interrupt */
		devpriv->IntEnable &= ~DAC_READY;
		writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

		/*  Clear interrupt status */
		devpriv->IntStatus |= DAC_READY;
		writew(devpriv->IntStatus,
		       devpriv->io_addr + ICP_MULTI_INT_STAT);

		/*  Clear data received */
		devpriv->ao_data[chan] = 0;

		return -ETIME;

dac_ready:
		/*  Write data to analogue output data register */
		writew(data[n], devpriv->io_addr + ICP_MULTI_AO);

		/*  Set DAC_ST bit to write the data to selected channel */
		devpriv->DacCmdStatus |= DAC_ST;
		writew(devpriv->DacCmdStatus,
		       devpriv->io_addr + ICP_MULTI_DAC_CSR);
		devpriv->DacCmdStatus &= ~DAC_ST;

		/*  Save analogue output data */
		devpriv->ao_data[chan] = data[n];
	}

	return n;
}

static int icp_multi_insn_read_ao(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct icp_multi_private *devpriv = dev->private;
	int n, chan;

	/*  Get channel number */
	chan = CR_CHAN(insn->chanspec);

	/*  Read analogue outputs */
	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_data[chan];

	return n;
}

static int icp_multi_insn_bits_di(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct icp_multi_private *devpriv = dev->private;

	data[1] = readw(devpriv->io_addr + ICP_MULTI_DI);

	return insn->n;
}

static int icp_multi_insn_bits_do(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	struct icp_multi_private *devpriv = dev->private;

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		printk(KERN_DEBUG "Digital outputs = %4x \n", s->state);

		writew(s->state, devpriv->io_addr + ICP_MULTI_DO);
	}

	data[1] = readw(devpriv->io_addr + ICP_MULTI_DI);

	return insn->n;
}

static int icp_multi_insn_read_ctr(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	return 0;
}

static int icp_multi_insn_write_ctr(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	return 0;
}

static irqreturn_t interrupt_service_icp_multi(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct icp_multi_private *devpriv = dev->private;
	int int_no;

	/*  Is this interrupt from our board? */
	int_no = readw(devpriv->io_addr + ICP_MULTI_INT_STAT) & Status_IRQ;
	if (!int_no)
		/*  No, exit */
		return IRQ_NONE;

	/*  Determine which interrupt is active & handle it */
	switch (int_no) {
	case ADC_READY:
		break;
	case DAC_READY:
		break;
	case DOUT_ERROR:
		break;
	case DIN_STATUS:
		break;
	case CIE0:
		break;
	case CIE1:
		break;
	case CIE2:
		break;
	case CIE3:
		break;
	default:
		break;

	}

	return IRQ_HANDLED;
}

#if 0
static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int *chanlist, unsigned int n_chan)
{
	unsigned int i;

	/*  Check that we at least have one channel to check */
	if (n_chan < 1) {
		comedi_error(dev, "range/channel list is empty!");
		return 0;
	}
	/*  Check all channels */
	for (i = 0; i < n_chan; i++) {
		/*  Check that channel number is < maximum */
		if (CR_AREF(chanlist[i]) == AREF_DIFF) {
			if (CR_CHAN(chanlist[i]) > (s->nchan / 2)) {
				comedi_error(dev,
					     "Incorrect differential ai ch-nr");
				return 0;
			}
		} else {
			if (CR_CHAN(chanlist[i]) > s->n_chan) {
				comedi_error(dev,
					     "Incorrect ai channel number");
				return 0;
			}
		}
	}
	return 1;
}
#endif

static int icp_multi_reset(struct comedi_device *dev)
{
	struct icp_multi_private *devpriv = dev->private;
	unsigned int i;

	/*  Clear INT enables and requests */
	writew(0, devpriv->io_addr + ICP_MULTI_INT_EN);
	writew(0x00ff, devpriv->io_addr + ICP_MULTI_INT_STAT);

	/* Set DACs to 0..5V range and 0V output */
	for (i = 0; i < 4; i++) {
		devpriv->DacCmdStatus &= 0xfcce;

		/*  Set channel number */
		devpriv->DacCmdStatus |= (i << 8);

		/*  Output 0V */
		writew(0, devpriv->io_addr + ICP_MULTI_AO);

		/*  Set start conversion bit */
		devpriv->DacCmdStatus |= DAC_ST;

		/*  Output to command / status register */
		writew(devpriv->DacCmdStatus,
			devpriv->io_addr + ICP_MULTI_DAC_CSR);

		/*  Delay to allow DAC time to recover */
		udelay(1);
	}

	/* Digital outputs to 0 */
	writew(0, devpriv->io_addr + ICP_MULTI_DO);

	return 0;
}

static int icp_multi_auto_attach(struct comedi_device *dev,
					   unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct icp_multi_private *devpriv;
	struct comedi_subdevice *s;
	resource_size_t iobase;
	int ret;

	dev->board_name = dev->driver->driver_name;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(pcidev, dev->board_name);
	if (ret)
		return ret;
	iobase = pci_resource_start(pcidev, 2);
	dev->iobase = iobase;

	devpriv->io_addr = ioremap(iobase, ICP_MULTI_SIZE);
	if (!devpriv->io_addr)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 5);
	if (ret)
		return ret;

	icp_multi_reset(dev);

	if (pcidev->irq) {
		ret = request_irq(pcidev->irq, interrupt_service_icp_multi,
				  IRQF_SHARED, dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_COMMON | SDF_GROUND | SDF_DIFF;
	s->n_chan = 16;
	s->maxdata = 0x0fff;
	s->len_chanlist = 16;
	s->range_table = &range_analog;
	s->insn_read = icp_multi_insn_read_ai;

	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 0x0fff;
	s->len_chanlist = 4;
	s->range_table = &range_analog;
	s->insn_write = icp_multi_insn_write_ao;
	s->insn_read = icp_multi_insn_read_ao;

	s = &dev->subdevices[2];
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->len_chanlist = 16;
	s->range_table = &range_digital;
	s->io_bits = 0;
	s->insn_bits = icp_multi_insn_bits_di;

	s = &dev->subdevices[3];
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->len_chanlist = 8;
	s->range_table = &range_digital;
	s->io_bits = 0xff;
	s->state = 0;
	s->insn_bits = icp_multi_insn_bits_do;

	s = &dev->subdevices[4];
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
	s->n_chan = 4;
	s->maxdata = 0xffff;
	s->len_chanlist = 4;
	s->state = 0;
	s->insn_read = icp_multi_insn_read_ctr;
	s->insn_write = icp_multi_insn_write_ctr;

	devpriv->valid = 1;

	dev_info(dev->class_dev, "%s attached, irq %sabled\n",
		dev->board_name, dev->irq ? "en" : "dis");

	return 0;
}

static void icp_multi_detach(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	struct icp_multi_private *devpriv = dev->private;

	if (devpriv)
		if (devpriv->valid)
			icp_multi_reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv && devpriv->io_addr)
		iounmap(devpriv->io_addr);
	if (pcidev) {
		if (dev->iobase)
			comedi_pci_disable(pcidev);
	}
}

static struct comedi_driver icp_multi_driver = {
	.driver_name	= "icp_multi",
	.module		= THIS_MODULE,
	.auto_attach	= icp_multi_auto_attach,
	.detach		= icp_multi_detach,
};

static int icp_multi_pci_probe(struct pci_dev *dev,
			       const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &icp_multi_driver, id->driver_data);
}

static DEFINE_PCI_DEVICE_TABLE(icp_multi_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ICP, PCI_DEVICE_ID_ICP_MULTI) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, icp_multi_pci_table);

static struct pci_driver icp_multi_pci_driver = {
	.name		= "icp_multi",
	.id_table	= icp_multi_pci_table,
	.probe		= icp_multi_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(icp_multi_driver, icp_multi_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
