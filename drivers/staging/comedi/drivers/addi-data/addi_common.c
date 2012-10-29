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

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

You should also find the complete GPL in the COPYING file accompanying this source code.

@endverbatim
*/
/*

  +-----------------------------------------------------------------------+
  | (C) ADDI-DATA GmbH          Dieselstrasse 3      D-77833 Ottersweier  |
  +-----------------------------------------------------------------------+
  | Tel : +49 (0) 7223/9493-0     | email    : info@addi-data.com         |
  | Fax : +49 (0) 7223/9493-92    | Internet : http://www.addi-data.com   |
  +-----------------------------------------------------------------------+
  | Project   : ADDI DATA         | Compiler : GCC 		          |
  | Modulname : addi_common.c     | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Author    :           | Date     :                    		  |
  +-----------------------------------------------------------------------+
  | Description : ADDI COMMON Main Module                                 |
  +-----------------------------------------------------------------------+
  | CONFIG OPTIONS                                                        |
  |	option[0] - PCI bus number - if bus number and slot number are 0, |
  |			         then driver search for first unused card |
  |	option[1] - PCI slot number                                       |
  |							                  |
  |	option[2] = 0  - DMA ENABLE                                       |
  |               = 1  - DMA DISABLE                                      |
  +----------+-----------+------------------------------------------------+
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

	w_Data = w_EepromReadWord(devpriv->i_IobaseAmcc,
		this_board->pc_EepromChip, 0x100 + (2 * w_Address));
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

static int i_ADDI_Attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv;
	struct comedi_subdevice *s;
	int ret, pages, i, n_subdevices;
	unsigned int dw_Dummy;
	resource_size_t io_addr[5];
	unsigned int irq;
	resource_size_t iobase_a, iobase_main, iobase_addon, iobase_reserved;
	struct pcilst_struct *card = NULL;
	unsigned char pci_bus, pci_slot, pci_func;
	int i_Dma = 0;

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	if (!pci_list_builded) {
		v_pci_card_list_init(this_board->i_VendorId, 1);	/* 1 for displaying the list.. */
		pci_list_builded = 1;
	}
	/* printk("comedi%d: "ADDIDATA_DRIVER_NAME": board=%s",dev->minor,this_board->pc_DriverName); */

	if ((this_board->i_Dma) && (it->options[2] == 0)) {
		i_Dma = 1;
	}

	card = ptr_select_and_alloc_pci_card(this_board->i_VendorId,
					     this_board->i_DeviceId,
					     it->options[0],
					     it->options[1], i_Dma);

	if (card == NULL)
		return -EIO;

	devpriv->allocated = 1;

	if ((i_pci_card_data(card, &pci_bus, &pci_slot, &pci_func, &io_addr[0],
				&irq)) < 0) {
		i_pci_card_free(card);
		printk(" - Can't get AMCC data!\n");
		return -EIO;
	}

	iobase_a = io_addr[0];
	iobase_main = io_addr[1];
	iobase_addon = io_addr[2];
	iobase_reserved = io_addr[3];
	printk("\nBus %d: Slot %d: Funct%d\nBase0: 0x%8llx\nBase1: 0x%8llx\nBase2: 0x%8llx\nBase3: 0x%8llx\n", pci_bus, pci_slot, pci_func, (unsigned long long)io_addr[0], (unsigned long long)io_addr[1], (unsigned long long)io_addr[2], (unsigned long long)io_addr[3]);

	if ((this_board->pc_EepromChip == NULL)
		|| (strcmp(this_board->pc_EepromChip, ADDIDATA_9054) != 0)) {
	   /************************************/
		/* Test if more that 1 address used */
	   /************************************/

		if (this_board->i_IorangeBase1 != 0) {
			dev->iobase = (unsigned long)iobase_main;	/*  DAQ base address... */
		} else {
			dev->iobase = (unsigned long)iobase_a;	/*  DAQ base address... */
		}

		dev->board_name = this_board->pc_DriverName;
		devpriv->amcc = card;
		devpriv->iobase = (int) dev->iobase;
		devpriv->i_IobaseAmcc = (int) iobase_a;	/* AMCC base address... */
		devpriv->i_IobaseAddon = (int) iobase_addon;	/* ADD ON base address.... */
		devpriv->i_IobaseReserved = (int) iobase_reserved;
	} else {
		dev->board_name = this_board->pc_DriverName;
		dev->iobase = (unsigned long)io_addr[2];
		devpriv->amcc = card;
		devpriv->iobase = (int) io_addr[2];
		devpriv->i_IobaseReserved = (int) io_addr[3];
		printk("\nioremap begin");
		devpriv->dw_AiBase = ioremap(io_addr[3],
					     this_board->i_IorangeBase3);
		printk("\nioremap end");
	}

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

	if (irq > 0) {
		if (request_irq(irq, v_ADDI_Interrupt, IRQF_SHARED,
				this_board->pc_DriverName, dev) < 0) {
			printk(", unable to allocate IRQ %u, DISABLING IT",
				irq);
			irq = 0;	/* Can't use IRQ */
		} else {
			printk("\nirq=%u", irq);
		}
	} else {
		printk(", IRQ disabled");
	}

	printk("\nOption %d %d %d\n", it->options[0], it->options[1],
		it->options[2]);
	dev->irq = irq;

	/*  Read eepeom and fill addi_board Structure */

	if (this_board->i_PCIEeprom) {
		printk("\nPCI Eeprom used");
		if (!(strcmp(this_board->pc_EepromChip, "S5920"))) {
			/*  Set 3 wait stait */
			if (!(strcmp(this_board->pc_DriverName, "apci035"))) {
				outl(0x80808082, devpriv->i_IobaseAmcc + 0x60);
			} else {
				outl(0x83838383, devpriv->i_IobaseAmcc + 0x60);
			}
			/*  Enable the interrupt for the controller */
			dw_Dummy = inl(devpriv->i_IobaseAmcc + 0x38);
			outl(dw_Dummy | 0x2000, devpriv->i_IobaseAmcc + 0x38);
			printk("\nEnable the interrupt for the controller");
		}
		printk("\nRead Eeprom");
		i_EepromReadMainHeader(io_addr[0], this_board->pc_EepromChip,
			dev);
	} else {
		printk("\nPCI Eeprom unused");
	}

	if (it->options[2] > 0) {
		devpriv->us_UseDma = ADDI_DISABLE;
	} else {
		devpriv->us_UseDma = ADDI_ENABLE;
	}

	if (devpriv->s_EeParameters.i_Dma) {
		printk("\nDMA used");
		if (devpriv->us_UseDma == ADDI_ENABLE) {
			/*  alloc DMA buffers */
			devpriv->b_DmaDoubleBuffer = 0;
			for (i = 0; i < 2; i++) {
				for (pages = 4; pages >= 0; pages--) {
					devpriv->ul_DmaBufferVirtual[i] =
						(void *) __get_free_pages(GFP_KERNEL, pages);

					if (devpriv->ul_DmaBufferVirtual[i])
						break;
				}
				if (devpriv->ul_DmaBufferVirtual[i]) {
					devpriv->ui_DmaBufferPages[i] = pages;
					devpriv->ui_DmaBufferSize[i] =
						PAGE_SIZE * pages;
					devpriv->ui_DmaBufferSamples[i] =
						devpriv->
						ui_DmaBufferSize[i] >> 1;
					devpriv->ul_DmaBufferHw[i] =
						virt_to_bus((void *)devpriv->
						ul_DmaBufferVirtual[i]);
				}
			}
			if (!devpriv->ul_DmaBufferVirtual[0]) {
				printk
					(", Can't allocate DMA buffer, DMA disabled!");
				devpriv->us_UseDma = ADDI_DISABLE;
			}

			if (devpriv->ul_DmaBufferVirtual[1]) {
				devpriv->b_DmaDoubleBuffer = 1;
			}
		}

		if ((devpriv->us_UseDma == ADDI_ENABLE)) {
			printk("\nDMA ENABLED\n");
		} else {
			printk("\nDMA DISABLED\n");
		}
	}

	if (!strcmp(this_board->pc_DriverName, "apci1710")) {
#ifdef CONFIG_APCI_1710
		i_ADDI_AttachPCI1710(dev);

		/*  save base address */
		devpriv->s_BoardInfos.ui_Address = io_addr[2];
#endif
	} else {
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

			/* Set the initialisation flag */
			devpriv->b_AiInitialisation = 1;

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
	}

	printk("\ni_ADDI_Attach end\n");
	i_ADDI_Reset(dev);
	devpriv->b_ValidDriver = 1;
	return 0;
}

static void i_ADDI_Detach(struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;

	if (devpriv) {
		if (devpriv->b_ValidDriver)
			i_ADDI_Reset(dev);
		if (dev->irq)
			free_irq(dev->irq, dev);
		if ((this_board->pc_EepromChip == NULL) ||
		    (strcmp(this_board->pc_EepromChip, ADDIDATA_9054) != 0)) {
			if (devpriv->allocated)
				i_pci_card_free(devpriv->amcc);
			if (devpriv->ul_DmaBufferVirtual[0]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[0],
					devpriv->ui_DmaBufferPages[0]);
			}
			if (devpriv->ul_DmaBufferVirtual[1]) {
				free_pages((unsigned long)devpriv->
					ul_DmaBufferVirtual[1],
					devpriv->ui_DmaBufferPages[1]);
			}
		} else {
			iounmap(devpriv->dw_AiBase);
			if (devpriv->allocated)
				i_pci_card_free(devpriv->amcc);
		}
		if (pci_list_builded) {
			v_pci_card_list_cleanup(this_board->i_VendorId);
			pci_list_builded = 0;
		}
	}
}

static struct comedi_driver addi_driver = {
	.driver_name	= ADDIDATA_DRIVER_NAME,
	.module		= THIS_MODULE,
	.attach		= i_ADDI_Attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(boardtypes),
	.board_name	= &boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit addi_pci_probe(struct pci_dev *dev,
				    const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &addi_driver);
}

static void __devexit addi_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver addi_pci_driver = {
	.name		= ADDIDATA_DRIVER_NAME,
	.id_table	= addi_apci_tbl,
	.probe		= &addi_pci_probe,
	.remove		= __devexit_p(&addi_pci_remove),
};
module_comedi_pci_driver(addi_driver, addi_pci_driver);
