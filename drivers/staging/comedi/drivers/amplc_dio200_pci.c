/* comedi/drivers/amplc_dio200_pci.c

    Driver for Amplicon PCI215, PCI272, PCIe215, PCIe236, PCIe296.

    Copyright (C) 2005-2013 MEV Ltd. <http://www.mev.co.uk/>

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998,2000 David A. Schleef <ds@schleef.org>

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
 * Driver: amplc_dio200_pci
 * Description: Amplicon 200 Series PCI Digital I/O
 * Author: Ian Abbott <abbotti@mev.co.uk>
 * Devices: [Amplicon] PCI215 (amplc_dio200_pci), PCIe215, PCIe236,
 *   PCI272, PCIe296
 * Updated: Mon, 18 Mar 2013 15:03:50 +0000
 * Status: works
 *
 * Configuration options:
 *   none
 *
 * Manual configuration of PCI(e) cards is not supported; they are configured
 * automatically.
 *
 * SUBDEVICES
 *
 *                     PCI215         PCIe215        PCIe236
 *                  -------------  -------------  -------------
 *   Subdevices           5              8              8
 *    0                 PPI-X          PPI-X          PPI-X
 *    1                 PPI-Y          UNUSED         UNUSED
 *    2                 CTR-Z1         PPI-Y          UNUSED
 *    3                 CTR-Z2         UNUSED         UNUSED
 *    4               INTERRUPT        CTR-Z1         CTR-Z1
 *    5                                CTR-Z2         CTR-Z2
 *    6                                TIMER          TIMER
 *    7                              INTERRUPT      INTERRUPT
 *
 *
 *                     PCI272         PCIe296
 *                  -------------  -------------
 *   Subdevices           4              8
 *    0                 PPI-X          PPI-X1
 *    1                 PPI-Y          PPI-X2
 *    2                 PPI-Z          PPI-Y1
 *    3               INTERRUPT        PPI-Y2
 *    4                                CTR-Z1
 *    5                                CTR-Z2
 *    6                                TIMER
 *    7                              INTERRUPT
 *
 * Each PPI is a 8255 chip providing 24 DIO channels.  The DIO channels
 * are configurable as inputs or outputs in four groups:
 *
 *   Port A  - channels  0 to  7
 *   Port B  - channels  8 to 15
 *   Port CL - channels 16 to 19
 *   Port CH - channels 20 to 23
 *
 * Only mode 0 of the 8255 chips is supported.
 *
 * Each CTR is a 8254 chip providing 3 16-bit counter channels.  Each
 * channel is configured individually with INSN_CONFIG instructions.  The
 * specific type of configuration instruction is specified in data[0].
 * Some configuration instructions expect an additional parameter in
 * data[1]; others return a value in data[1].  The following configuration
 * instructions are supported:
 *
 *   INSN_CONFIG_SET_COUNTER_MODE.  Sets the counter channel's mode and
 *     BCD/binary setting specified in data[1].
 *
 *   INSN_CONFIG_8254_READ_STATUS.  Reads the status register value for the
 *     counter channel into data[1].
 *
 *   INSN_CONFIG_SET_CLOCK_SRC.  Sets the counter channel's clock source as
 *     specified in data[1] (this is a hardware-specific value).  Not
 *     supported on PC214E.  For the other boards, valid clock sources are
 *     0 to 7 as follows:
 *
 *       0.  CLK n, the counter channel's dedicated CLK input from the SK1
 *         connector.  (N.B. for other values, the counter channel's CLKn
 *         pin on the SK1 connector is an output!)
 *       1.  Internal 10 MHz clock.
 *       2.  Internal 1 MHz clock.
 *       3.  Internal 100 kHz clock.
 *       4.  Internal 10 kHz clock.
 *       5.  Internal 1 kHz clock.
 *       6.  OUT n-1, the output of counter channel n-1 (see note 1 below).
 *       7.  Ext Clock, the counter chip's dedicated Ext Clock input from
 *         the SK1 connector.  This pin is shared by all three counter
 *         channels on the chip.
 *
 *     For the PCIe boards, clock sources in the range 0 to 31 are allowed
 *     and the following additional clock sources are defined:
 *
 *       8.  HIGH logic level.
 *       9.  LOW logic level.
 *      10.  "Pattern present" signal.
 *      11.  Internal 20 MHz clock.
 *
 *   INSN_CONFIG_GET_CLOCK_SRC.  Returns the counter channel's current
 *     clock source in data[1].  For internal clock sources, data[2] is set
 *     to the period in ns.
 *
 *   INSN_CONFIG_SET_GATE_SRC.  Sets the counter channel's gate source as
 *     specified in data[2] (this is a hardware-specific value).  Not
 *     supported on PC214E.  For the other boards, valid gate sources are 0
 *     to 7 as follows:
 *
 *       0.  VCC (internal +5V d.c.), i.e. gate permanently enabled.
 *       1.  GND (internal 0V d.c.), i.e. gate permanently disabled.
 *       2.  GAT n, the counter channel's dedicated GAT input from the SK1
 *         connector.  (N.B. for other values, the counter channel's GATn
 *         pin on the SK1 connector is an output!)
 *       3.  /OUT n-2, the inverted output of counter channel n-2 (see note
 *         2 below).
 *       4.  Reserved.
 *       5.  Reserved.
 *       6.  Reserved.
 *       7.  Reserved.
 *
 *     For the PCIe boards, gate sources in the range 0 to 31 are allowed;
 *     the following additional clock sources and clock sources 6 and 7 are
 *     (re)defined:
 *
 *       6.  /GAT n, negated version of the counter channel's dedicated
 *         GAT input (negated version of gate source 2).
 *       7.  OUT n-2, the non-inverted output of counter channel n-2
 *         (negated version of gate source 3).
 *       8.  "Pattern present" signal, HIGH while pattern present.
 *       9.  "Pattern occurred" latched signal, latches HIGH when pattern
 *         occurs.
 *      10.  "Pattern gone away" latched signal, latches LOW when pattern
 *         goes away after it occurred.
 *      11.  Negated "pattern present" signal, LOW while pattern present
 *         (negated version of gate source 8).
 *      12.  Negated "pattern occurred" latched signal, latches LOW when
 *         pattern occurs (negated version of gate source 9).
 *      13.  Negated "pattern gone away" latched signal, latches LOW when
 *         pattern goes away after it occurred (negated version of gate
 *         source 10).
 *
 *   INSN_CONFIG_GET_GATE_SRC.  Returns the counter channel's current gate
 *     source in data[2].
 *
 * Clock and gate interconnection notes:
 *
 *   1.  Clock source OUT n-1 is the output of the preceding channel on the
 *   same counter subdevice if n > 0, or the output of channel 2 on the
 *   preceding counter subdevice (see note 3) if n = 0.
 *
 *   2.  Gate source /OUT n-2 is the inverted output of channel 0 on the
 *   same counter subdevice if n = 2, or the inverted output of channel n+1
 *   on the preceding counter subdevice (see note 3) if n < 2.
 *
 *   3.  The counter subdevices are connected in a ring, so the highest
 *   counter subdevice precedes the lowest.
 *
 * The 'TIMER' subdevice is a free-running 32-bit timer subdevice.
 *
 * The 'INTERRUPT' subdevice pretends to be a digital input subdevice.  The
 * digital inputs come from the interrupt status register.  The number of
 * channels matches the number of interrupt sources.  The PC214E does not
 * have an interrupt status register; see notes on 'INTERRUPT SOURCES'
 * below.
 *
 * INTERRUPT SOURCES
 *
 *                     PCI215         PCIe215        PCIe236
 *                  -------------  -------------  -------------
 *   Sources              6              6              6
 *    0               PPI-X-C0       PPI-X-C0       PPI-X-C0
 *    1               PPI-X-C3       PPI-X-C3       PPI-X-C3
 *    2               PPI-Y-C0       PPI-Y-C0        unused
 *    3               PPI-Y-C3       PPI-Y-C3        unused
 *    4              CTR-Z1-OUT1    CTR-Z1-OUT1    CTR-Z1-OUT1
 *    5              CTR-Z2-OUT1    CTR-Z2-OUT1    CTR-Z2-OUT1
 *
 *                     PCI272         PCIe296
 *                  -------------  -------------
 *   Sources              6              6
 *    0               PPI-X-C0       PPI-X1-C0
 *    1               PPI-X-C3       PPI-X1-C3
 *    2               PPI-Y-C0       PPI-Y1-C0
 *    3               PPI-Y-C3       PPI-Y1-C3
 *    4               PPI-Z-C0      CTR-Z1-OUT1
 *    5               PPI-Z-C3      CTR-Z2-OUT1
 *
 * When an interrupt source is enabled in the interrupt source enable
 * register, a rising edge on the source signal latches the corresponding
 * bit to 1 in the interrupt status register.
 *
 * When the interrupt status register value as a whole (actually, just the
 * 6 least significant bits) goes from zero to non-zero, the board will
 * generate an interrupt.  The interrupt will remain asserted until the
 * interrupt status register is cleared to zero.  To clear a bit to zero in
 * the interrupt status register, the corresponding interrupt source must
 * be disabled in the interrupt source enable register (there is no
 * separate interrupt clear register).
 *
 * COMMANDS
 *
 * The driver supports a read streaming acquisition command on the
 * 'INTERRUPT' subdevice.  The channel list selects the interrupt sources
 * to be enabled.  All channels will be sampled together (convert_src ==
 * TRIG_NOW).  The scan begins a short time after the hardware interrupt
 * occurs, subject to interrupt latencies (scan_begin_src == TRIG_EXT,
 * scan_begin_arg == 0).  The value read from the interrupt status register
 * is packed into a short value, one bit per requested channel, in the
 * order they appear in the channel list.
 */

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedi_pci.h"

#include "amplc_dio200.h"

/*
 * Board descriptions.
 */

enum dio200_pci_model {
	pci215_model,
	pci272_model,
	pcie215_model,
	pcie236_model,
	pcie296_model
};

static const struct dio200_board dio200_pci_boards[] = {
	[pci215_model] = {
		.name		= "pci215",
		.mainbar	= 2,
		.n_subdevs	= 5,
		.sdtype		= {
			sd_8255, sd_8255, sd_8254, sd_8254, sd_intr
		},
		.sdinfo		= { 0x00, 0x08, 0x10, 0x14, 0x3f },
		.has_int_sce	= true,
		.has_clk_gat_sce = true,
	},
	[pci272_model] = {
		.name		= "pci272",
		.mainbar	= 2,
		.n_subdevs	= 4,
		.sdtype		= {
			sd_8255, sd_8255, sd_8255, sd_intr
		},
		.sdinfo		= { 0x00, 0x08, 0x10, 0x3f },
		.has_int_sce	= true,
	},
	[pcie215_model] = {
		.name		= "pcie215",
		.mainbar	= 1,
		.n_subdevs	= 8,
		.sdtype		= {
			sd_8255, sd_none, sd_8255, sd_none,
			sd_8254, sd_8254, sd_timer, sd_intr
		},
		.sdinfo		= {
			0x00, 0x00, 0x08, 0x00, 0x10, 0x14, 0x00, 0x3f
		},
		.has_int_sce	= true,
		.has_clk_gat_sce = true,
		.is_pcie	= true,
	},
	[pcie236_model] = {
		.name		= "pcie236",
		.mainbar	= 1,
		.n_subdevs	= 8,
		.sdtype		= {
			sd_8255, sd_none, sd_none, sd_none,
			sd_8254, sd_8254, sd_timer, sd_intr
		},
		.sdinfo		= {
			0x00, 0x00, 0x00, 0x00, 0x10, 0x14, 0x00, 0x3f
		},
		.has_int_sce	= true,
		.has_clk_gat_sce = true,
		.is_pcie	= true,
	},
	[pcie296_model] = {
		.name		= "pcie296",
		.mainbar	= 1,
		.n_subdevs	= 8,
		.sdtype		= {
			sd_8255, sd_8255, sd_8255, sd_8255,
			sd_8254, sd_8254, sd_timer, sd_intr
		},
		.sdinfo		= {
			0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x00, 0x3f
		},
		.has_int_sce	= true,
		.has_clk_gat_sce = true,
		.is_pcie	= true,
	},
};

/*
 * This function does some special set-up for the PCIe boards
 * PCIe215, PCIe236, PCIe296.
 */
static int dio200_pcie_board_setup(struct comedi_device *dev)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	void __iomem *brbase;

	/*
	 * The board uses Altera Cyclone IV with PCI-Express hard IP.
	 * The FPGA configuration has the PCI-Express Avalon-MM Bridge
	 * Control registers in PCI BAR 0, offset 0, and the length of
	 * these registers is 0x4000.
	 *
	 * We need to write 0x80 to the "Avalon-MM to PCI-Express Interrupt
	 * Enable" register at offset 0x50 to allow generation of PCIe
	 * interrupts when RXmlrq_i is asserted in the SOPC Builder system.
	 */
	if (pci_resource_len(pcidev, 0) < 0x4000) {
		dev_err(dev->class_dev, "error! bad PCI region!\n");
		return -EINVAL;
	}
	brbase = pci_ioremap_bar(pcidev, 0);
	if (!brbase) {
		dev_err(dev->class_dev, "error! failed to map registers!\n");
		return -ENOMEM;
	}
	writel(0x80, brbase + 0x50);
	iounmap(brbase);
	/* Enable "enhanced" features of board. */
	amplc_dio200_set_enhance(dev, 1);
	return 0;
}

static int dio200_pci_auto_attach(struct comedi_device *dev,
				  unsigned long context_model)
{
	struct pci_dev *pci_dev = comedi_to_pci_dev(dev);
	const struct dio200_board *board = NULL;
	unsigned int bar;
	int ret;

	if (context_model < ARRAY_SIZE(dio200_pci_boards))
		board = &dio200_pci_boards[context_model];
	if (!board)
		return -EINVAL;
	dev->board_ptr = board;
	dev->board_name = board->name;

	dev_info(dev->class_dev, "%s: attach pci %s (%s)\n",
		 dev->driver->driver_name, pci_name(pci_dev), dev->board_name);

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	bar = board->mainbar;
	if (pci_resource_flags(pci_dev, bar) & IORESOURCE_MEM) {
		dev->mmio = pci_ioremap_bar(pci_dev, bar);
		if (!dev->mmio) {
			dev_err(dev->class_dev,
				"error! cannot remap registers\n");
			return -ENOMEM;
		}
	} else {
		dev->iobase = pci_resource_start(pci_dev, bar);
	}

	if (board->is_pcie) {
		ret = dio200_pcie_board_setup(dev);
		if (ret < 0)
			return ret;
	}

	return amplc_dio200_common_attach(dev, pci_dev->irq, IRQF_SHARED);
}

static struct comedi_driver dio200_pci_comedi_driver = {
	.driver_name	= "amplc_dio200_pci",
	.module		= THIS_MODULE,
	.auto_attach	= dio200_pci_auto_attach,
	.detach		= comedi_pci_detach,
};

static const struct pci_device_id dio200_pci_table[] = {
	{ PCI_VDEVICE(AMPLICON, 0x000b), pci215_model },
	{ PCI_VDEVICE(AMPLICON, 0x000a), pci272_model },
	{ PCI_VDEVICE(AMPLICON, 0x0011), pcie236_model },
	{ PCI_VDEVICE(AMPLICON, 0x0012), pcie215_model },
	{ PCI_VDEVICE(AMPLICON, 0x0014), pcie296_model },
	{0}
};

MODULE_DEVICE_TABLE(pci, dio200_pci_table);

static int dio200_pci_probe(struct pci_dev *dev, const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &dio200_pci_comedi_driver,
				      id->driver_data);
}

static struct pci_driver dio200_pci_pci_driver = {
	.name		= "amplc_dio200_pci",
	.id_table	= dio200_pci_table,
	.probe		= dio200_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(dio200_pci_comedi_driver, dio200_pci_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for Amplicon 200 Series PCI(e) DIO boards");
MODULE_LICENSE("GPL");
