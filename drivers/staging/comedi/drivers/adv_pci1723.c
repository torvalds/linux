/*******************************************************************************
   comedi/drivers/pci1723.c

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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

*******************************************************************************/
/*
Driver: adv_pci1723
Description: Advantech PCI-1723
Author: yonggang <rsmgnu@gmail.com>, Ian Abbott <abbotti@mev.co.uk>
Devices: [Advantech] PCI-1723 (adv_pci1723)
Updated: Mon, 14 Apr 2008 15:12:56 +0100
Status: works

Configuration Options:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)

  If bus/slot is not specified, the first supported
  PCI device found will be used.

Subdevice 0 is 8-channel AO, 16-bit, range +/- 10 V.

Subdevice 1 is 16-channel DIO.  The channels are configurable as input or
output in 2 groups (0 to 7, 8 to 15).  Configuring any channel implicitly
configures all channels in the same group.

TODO:

1. Add the two milliamp ranges to the AO subdevice (0 to 20 mA, 4 to 20 mA).
2. Read the initial ranges and values of the AO subdevice at start-up instead
   of reinitializing them.
3. Implement calibration.
*/

#include "../comedidev.h"

#include "comedi_pci.h"

#define PCI_VENDOR_ID_ADVANTECH		0x13fe	/* Advantech PCI vendor ID */

/* hardware types of the cards */
#define TYPE_PCI1723 0

#define IORANGE_1723  0x2A

/* all the registers for the pci1723 board */
#define PCI1723_DA(N)   ((N)<<1)	/* W: D/A register N (0 to 7) */

#define PCI1723_SYN_SET  0x12		/* synchronized set register */
#define PCI1723_ALL_CHNNELE_SYN_STROBE  0x12
					/* synchronized status register */

#define PCI1723_RANGE_CALIBRATION_MODE 0x14
					/* range and calibration mode */
#define PCI1723_RANGE_CALIBRATION_STATUS 0x14
					/* range and calibration status */

#define PCI1723_CONTROL_CMD_CALIBRATION_FUN 0x16
						/*
						 * SADC control command for
						 * calibration function
						 */
#define PCI1723_STATUS_CMD_CALIBRATION_FUN 0x16
						/*
						 * SADC control status for
						 * calibration function
						 */

#define PCI1723_CALIBRATION_PARA_STROBE 0x18
					/* Calibration parameter strobe */

#define PCI1723_DIGITAL_IO_PORT_SET 0x1A	/* Digital I/O port setting */
#define PCI1723_DIGITAL_IO_PORT_MODE 0x1A	/* Digital I/O port mode */

#define PCI1723_WRITE_DIGITAL_OUTPUT_CMD 0x1C
					/* Write digital output command */
#define PCI1723_READ_DIGITAL_INPUT_DATA 0x1C	/* Read digital input data */

#define PCI1723_WRITE_CAL_CMD 0x1E		/* Write calibration command */
#define PCI1723_READ_CAL_STATUS 0x1E		/* Read calibration status */

#define PCI1723_SYN_STROBE 0x20			/* Synchronized strobe */

#define PCI1723_RESET_ALL_CHN_STROBE 0x22
					/* Reset all D/A channels strobe */

#define PCI1723_RESET_CAL_CONTROL_STROBE 0x24
						/*
						 * Reset the calibration
						 * controller strobe
						 */

#define PCI1723_CHANGE_CHA_OUTPUT_TYPE_STROBE 0x26
						/*
						 * Change D/A channels output
						 * type strobe
						 */

#define PCI1723_SELECT_CALIBRATION 0x28	/* Select the calibration Ref_V */

/* static unsigned short pci_list_builded=0;      =1 list of card is know */

static const struct comedi_lrange range_pci1723 = { 1, {
							BIP_RANGE(10)
							}
};

/*
 * Board descriptions for pci1723 boards.
 */
struct pci1723_board {
	const char *name;
	int vendor_id;		/* PCI vendor a device ID of card */
	int device_id;
	int iorange;
	char cardtype;
	int n_aochan;		/* num of D/A chans */
	int n_diochan;		/* num of DIO chans */
	int ao_maxdata;		/* resolution of D/A */
	const struct comedi_lrange *rangelist_ao;	/* rangelist for D/A */
};

static const struct pci1723_board boardtypes[] = {
	{
	 .name = "pci1723",
	 .vendor_id = PCI_VENDOR_ID_ADVANTECH,
	 .device_id = 0x1723,
	 .iorange = IORANGE_1723,
	 .cardtype = TYPE_PCI1723,
	 .n_aochan = 8,
	 .n_diochan = 16,
	 .ao_maxdata = 0xffff,
	 .rangelist_ao = &range_pci1723,
	 },
};

/* This structure is for data unique to this hardware driver. */
struct pci1723_private {
	int valid;		/* card is usable; */

	struct pci_dev *pcidev;
	unsigned char da_range[8];	/* D/A output range for each channel */

	short ao_data[8];	/* data output buffer */
};

/* The following macro to make it easy to access the private structure. */
#define devpriv ((struct pci1723_private *)dev->private)

#define this_board boardtypes

/*
 * The pci1723 card reset;
 */
static int pci1723_reset(struct comedi_device *dev)
{
	int i;
	DPRINTK("adv_pci1723 EDBG: BGN: pci1723_reset(...)\n");

	outw(0x01, dev->iobase + PCI1723_SYN_SET);
					       /* set synchronous output mode */

	for (i = 0; i < 8; i++) {
		/* set all outputs to 0V */
		devpriv->ao_data[i] = 0x8000;
		outw(devpriv->ao_data[i], dev->iobase + PCI1723_DA(i));
		/* set all ranges to +/- 10V */
		devpriv->da_range[i] = 0;
		outw(((devpriv->da_range[i] << 4) | i),
		     PCI1723_RANGE_CALIBRATION_MODE);
	}

	outw(0, dev->iobase + PCI1723_CHANGE_CHA_OUTPUT_TYPE_STROBE);
							    /* update ranges */
	outw(0, dev->iobase + PCI1723_SYN_STROBE);	    /* update outputs */

	/* set asynchronous output mode */
	outw(0, dev->iobase + PCI1723_SYN_SET);

	DPRINTK("adv_pci1723 EDBG: END: pci1723_reset(...)\n");
	return 0;
}

static int pci1723_insn_read_ao(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int n, chan;

	chan = CR_CHAN(insn->chanspec);
	DPRINTK(" adv_PCI1723 DEBUG: pci1723_insn_read_ao() -----\n");
	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_data[chan];

	return n;
}

/*
  analog data output;
*/
static int pci1723_ao_write_winsn(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int n, chan;
	chan = CR_CHAN(insn->chanspec);

	DPRINTK("PCI1723: the pci1723_ao_write_winsn() ------\n");

	for (n = 0; n < insn->n; n++) {

		devpriv->ao_data[chan] = data[n];
		outw(data[n], dev->iobase + PCI1723_DA(chan));
	}

	return n;
}

/*
  digital i/o config/query
*/
static int pci1723_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	unsigned int mask;
	unsigned int bits;
	unsigned short dio_mode;

	mask = 1 << CR_CHAN(insn->chanspec);
	if (mask & 0x00FF)
		bits = 0x00FF;
	else
		bits = 0xFF00;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~bits;
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= bits;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & bits) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
	default:
		return -EINVAL;
	}

	/* update hardware DIO mode */
	dio_mode = 0x0000;	/* low byte output, high byte output */
	if ((s->io_bits & 0x00FF) == 0)
		dio_mode |= 0x0001;	/* low byte input */
	if ((s->io_bits & 0xFF00) == 0)
		dio_mode |= 0x0002;	/* high byte input */
	outw(dio_mode, dev->iobase + PCI1723_DIGITAL_IO_PORT_SET);
	return 1;
}

/*
  digital i/o bits read/write
*/
static int pci1723_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);
		outw(s->state, dev->iobase + PCI1723_WRITE_DIGITAL_OUTPUT_CMD);
	}
	data[1] = inw(dev->iobase + PCI1723_READ_DIGITAL_INPUT_DATA);
	return 2;
}

static int pci1723_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret, subdev, n_subdevices;
	struct pci_dev *pcidev;
	unsigned int iobase;
	unsigned char pci_bus, pci_slot, pci_func;
	int opt_bus, opt_slot;
	const char *errstr;

	printk(KERN_ERR "comedi%d: adv_pci1723: board=%s",
						dev->minor, this_board->name);

	opt_bus = it->options[0];
	opt_slot = it->options[1];

	ret = alloc_private(dev, sizeof(struct pci1723_private));
	if (ret < 0) {
		printk(" - Allocation failed!\n");
		return -ENOMEM;
	}

	/* Look for matching PCI device */
	errstr = "not found!";
	pcidev = NULL;
	while (NULL != (pcidev =
			pci_get_device(PCI_VENDOR_ID_ADVANTECH,
				       this_board->device_id, pcidev))) {
		/* Found matching vendor/device. */
		if (opt_bus || opt_slot) {
			/* Check bus/slot. */
			if (opt_bus != pcidev->bus->number
			    || opt_slot != PCI_SLOT(pcidev->devfn))
				continue;	/* no match */
		}
		/*
		 * Look for device that isn't in use.
		 * Enable PCI device and request regions.
		 */
		if (comedi_pci_enable(pcidev, "adv_pci1723")) {
			errstr =
			    "failed to enable PCI device and request regions!";
			continue;
		}
		break;
	}

	if (!pcidev) {
		if (opt_bus || opt_slot) {
			printk(KERN_ERR " - Card at b:s %d:%d %s\n",
						     opt_bus, opt_slot, errstr);
		} else {
			printk(KERN_ERR " - Card %s\n", errstr);
		}
		return -EIO;
	}

	pci_bus = pcidev->bus->number;
	pci_slot = PCI_SLOT(pcidev->devfn);
	pci_func = PCI_FUNC(pcidev->devfn);
	iobase = pci_resource_start(pcidev, 2);

	printk(KERN_ERR ", b:s:f=%d:%d:%d, io=0x%4x",
					   pci_bus, pci_slot, pci_func, iobase);

	dev->iobase = iobase;

	dev->board_name = this_board->name;
	devpriv->pcidev = pcidev;

	n_subdevices = 0;

	if (this_board->n_aochan)
		n_subdevices++;
	if (this_board->n_diochan)
		n_subdevices++;

	ret = alloc_subdevices(dev, n_subdevices);
	if (ret < 0) {
		printk(" - Allocation failed!\n");
		return ret;
	}

	pci1723_reset(dev);
	subdev = 0;
	if (this_board->n_aochan) {
		s = dev->subdevices + subdev;
		dev->write_subdev = s;
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->n_aochan;
		s->maxdata = this_board->ao_maxdata;
		s->len_chanlist = this_board->n_aochan;
		s->range_table = this_board->rangelist_ao;

		s->insn_write = pci1723_ao_write_winsn;
		s->insn_read = pci1723_insn_read_ao;

		/* read DIO config */
		switch (inw(dev->iobase + PCI1723_DIGITAL_IO_PORT_MODE)
								       & 0x03) {
		case 0x00:	/* low byte output, high byte output */
			s->io_bits = 0xFFFF;
			break;
		case 0x01:	/* low byte input, high byte output */
			s->io_bits = 0xFF00;
			break;
		case 0x02:	/* low byte output, high byte input */
			s->io_bits = 0x00FF;
			break;
		case 0x03:	/* low byte input, high byte input */
			s->io_bits = 0x0000;
			break;
		}
		/* read DIO port state */
		s->state = inw(dev->iobase + PCI1723_READ_DIGITAL_INPUT_DATA);

		subdev++;
	}

	if (this_board->n_diochan) {
		s = dev->subdevices + subdev;
		s->type = COMEDI_SUBD_DIO;
		s->subdev_flags =
		    SDF_READABLE | SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->n_diochan;
		s->maxdata = 1;
		s->len_chanlist = this_board->n_diochan;
		s->range_table = &range_digital;
		s->insn_config = pci1723_dio_insn_config;
		s->insn_bits = pci1723_dio_insn_bits;
		subdev++;
	}

	devpriv->valid = 1;

	pci1723_reset(dev);

	return 0;
}

static void pci1723_detach(struct comedi_device *dev)
{
	if (dev->private) {
		if (devpriv->valid)
			pci1723_reset(dev);
		if (devpriv->pcidev) {
			if (dev->iobase)
				comedi_pci_disable(devpriv->pcidev);
			pci_dev_put(devpriv->pcidev);
		}
	}
}

static struct comedi_driver adv_pci1723_driver = {
	.driver_name	= "adv_pci1723",
	.module		= THIS_MODULE,
	.attach		= pci1723_attach,
	.detach		= pci1723_detach,
};

static int __devinit adv_pci1723_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &adv_pci1723_driver);
}

static void __devexit adv_pci1723_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(adv_pci1723_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADVANTECH, 0x1723) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, adv_pci1723_pci_table);

static struct pci_driver adv_pci1723_pci_driver = {
	.name		= "adv_pci1723",
	.id_table	= adv_pci1723_pci_table,
	.probe		= adv_pci1723_pci_probe,
	.remove		= __devexit_p(adv_pci1723_pci_remove),
};
module_comedi_pci_driver(adv_pci1723_driver, adv_pci1723_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
