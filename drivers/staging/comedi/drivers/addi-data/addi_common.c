/**
@verbatim

Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.

	ADDI-DATA GmbH
	Dieselstrasse 3
	D-77833 Ottersweier
	Tel: +19(0)7223/9493-0
	Fax: +49(0)7223/9493-92
	http://www.addi-data.com
	info@addi-data.com

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

@endverbatim
*/
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project   : ADDI DATA         | Compiler : GCC                        |
  | Modulname : addi_common.c     | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Author    :           | Date     :                                    |
  +-----------------------------------------------------------------------+
  | Description : ADDI COMMON Main Module                                 |
  +-----------------------------------------------------------------------+
*/

#ifndef COMEDI_SUBD_TTLIO
#define COMEDI_SUBD_TTLIO   11	/* Digital Input Output But TTL */
#endif

static int i_ADDIDATA_InsnReadEeprom(struct comedi_device *dev,
				     struct comedi_subdevice *s,
				     struct comedi_insn *insn,
				     unsigned int *data)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;
	unsigned short w_Address = CR_CHAN(insn->chanspec);
	unsigned short w_Data;

	w_Data = addi_eeprom_readw(devpriv->i_IobaseAmcc,
		this_board->pc_EepromChip, 2 * w_Address);
	data[0] = w_Data;

	return insn->n;
}

static irqreturn_t v_ADDI_Interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	const struct addi_board *this_board = comedi_board(dev);

	this_board->interrupt(irq, d);
	return IRQ_RETVAL(1);
}

static int i_ADDI_Reset(struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);

	this_board->reset(dev);
	return 0;
}

static int addi_auto_attach(struct comedi_device *dev,
				      unsigned long context_unused)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, n_subdevices;
	unsigned int dw_Dummy;

	dev->board_name = this_board->pc_DriverName;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	if (this_board->i_IorangeBase1)
		dev->iobase = pci_resource_start(pcidev, 1);
	else
		dev->iobase = pci_resource_start(pcidev, 0);

	devpriv->iobase = dev->iobase;
	devpriv->i_IobaseAmcc = pci_resource_start(pcidev, 0);
	devpriv->i_IobaseAddon = pci_resource_start(pcidev, 2);
	devpriv->i_IobaseReserved = pci_resource_start(pcidev, 3);

	/* Initialize parameters that can be overridden in EEPROM */
	devpriv->s_EeParameters.i_NbrAiChannel = this_board->i_NbrAiChannel;
	devpriv->s_EeParameters.i_NbrAoChannel = this_board->i_NbrAoChannel;
	devpriv->s_EeParameters.i_AiMaxdata = this_board->i_AiMaxdata;
	devpriv->s_EeParameters.i_AoMaxdata = this_board->i_AoMaxdata;
	devpriv->s_EeParameters.i_NbrDiChannel = this_board->i_NbrDiChannel;
	devpriv->s_EeParameters.i_NbrDoChannel = this_board->i_NbrDoChannel;
	devpriv->s_EeParameters.i_DoMaxdata = this_board->i_DoMaxdata;
	devpriv->s_EeParameters.i_Dma = this_board->i_Dma;
	devpriv->s_EeParameters.i_Timer = this_board->i_Timer;
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
		this_board->ui_MinAcquisitiontimeNs;
	devpriv->s_EeParameters.ui_MinDelaytimeNs =
		this_board->ui_MinDelaytimeNs;

	/* ## */

	if (pcidev->irq > 0) {
		ret = request_irq(pcidev->irq, v_ADDI_Interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = pcidev->irq;
	}

	/*  Read eepeom and fill addi_board Structure */

	if (this_board->i_PCIEeprom) {
		if (!(strcmp(this_board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(dev->board_name, "apci035")))
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			else
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);

			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
		}
		addi_eeprom_read_info(dev, pci_resource_start(pcidev, 0));
	}

	n_subdevices = 7;
	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	/*  Allocate and Initialise AI Subdevice Structures */
	s = &dev->subdevices[0];
	if ((devpriv->s_EeParameters.i_NbrAiChannel)
		|| (this_board->i_NbrAiChannelDiff)) {
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags =
			SDF_READABLE | SDF_COMMON | SDF_GROUND
			| SDF_DIFF;
		if (devpriv->s_EeParameters.i_NbrAiChannel) {
			s->n_chan =
				devpriv->s_EeParameters.i_NbrAiChannel;
			devpriv->b_SingelDiff = 0;
		} else {
			s->n_chan = this_board->i_NbrAiChannelDiff;
			devpriv->b_SingelDiff = 1;
		}
		s->maxdata = devpriv->s_EeParameters.i_AiMaxdata;
		s->len_chanlist = this_board->i_AiChannelList;
		s->range_table = this_board->pr_AiRangelist;

		s->insn_config = this_board->ai_config;
		s->insn_read = this_board->ai_read;
		s->insn_write = this_board->ai_write;
		s->insn_bits = this_board->ai_bits;
		s->do_cmdtest = this_board->ai_cmdtest;
		s->do_cmd = this_board->ai_cmd;
		s->cancel = this_board->ai_cancel;

	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise AO Subdevice Structures */
	s = &dev->subdevices[1];
	if (devpriv->s_EeParameters.i_NbrAoChannel) {
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrAoChannel;
		s->maxdata = devpriv->s_EeParameters.i_AoMaxdata;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrAoChannel;
		s->range_table = this_board->pr_AoRangelist;
		s->insn_config = this_board->ao_config;
		s->insn_write = this_board->ao_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DI Subdevice Structures */
	s = &dev->subdevices[2];
	if (devpriv->s_EeParameters.i_NbrDiChannel) {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDiChannel;
		s->maxdata = 1;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrDiChannel;
		s->range_table = &range_digital;
		s->io_bits = 0;	/* all bits input */
		s->insn_config = this_board->di_config;
		s->insn_read = this_board->di_read;
		s->insn_write = this_board->di_write;
		s->insn_bits = this_board->di_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}
	/*  Allocate and Initialise DO Subdevice Structures */
	s = &dev->subdevices[3];
	if (devpriv->s_EeParameters.i_NbrDoChannel) {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags =
			SDF_READABLE | SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = devpriv->s_EeParameters.i_NbrDoChannel;
		s->maxdata = devpriv->s_EeParameters.i_DoMaxdata;
		s->len_chanlist =
			devpriv->s_EeParameters.i_NbrDoChannel;
		s->range_table = &range_digital;
		s->io_bits = 0xf;	/* all bits output */

		/* insn_config - for digital output memory */
		s->insn_config = this_board->do_config;
		s->insn_write = this_board->do_write;
		s->insn_bits = this_board->do_bits;
		s->insn_read = this_board->do_read;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise Timer Subdevice Structures */
	s = &dev->subdevices[4];
	if (devpriv->s_EeParameters.i_Timer) {
		s->type = COMEDI_SUBD_TIMER;
		s->subdev_flags = SDF_WRITEABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = 1;
		s->maxdata = 0;
		s->len_chanlist = 1;
		s->range_table = &range_digital;

		s->insn_write = this_board->timer_write;
		s->insn_read = this_board->timer_read;
		s->insn_config = this_board->timer_config;
		s->insn_bits = this_board->timer_bits;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/*  Allocate and Initialise TTL */
	s = &dev->subdevices[5];
	if (this_board->i_NbrTTLChannel) {
		s->type = COMEDI_SUBD_TTLIO;
		s->subdev_flags =
			SDF_WRITEABLE | SDF_READABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->i_NbrTTLChannel;
		s->maxdata = 1;
		s->io_bits = 0;	/* all bits input */
		s->len_chanlist = this_board->i_NbrTTLChannel;
		s->range_table = &range_digital;
		s->insn_config = this_board->ttl_config;
		s->insn_bits = this_board->ttl_bits;
		s->insn_read = this_board->ttl_read;
		s->insn_write = this_board->ttl_write;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	/* EEPROM */
	s = &dev->subdevices[6];
	if (this_board->i_PCIEeprom) {
		s->type = COMEDI_SUBD_MEMORY;
		s->subdev_flags = SDF_READABLE | SDF_INTERNAL;
		s->n_chan = 256;
		s->maxdata = 0xffff;
		s->insn_read = i_ADDIDATA_InsnReadEeprom;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	i_ADDI_Reset(dev);
	return 0;
}

static void i_ADDI_Detach(struct comedi_device *dev)
{
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (dev->iobase)
			i_ADDI_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
	}
	comedi_pci_disable(dev);
}
