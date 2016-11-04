/*
 * Comedi driver for NI PCI-MIO E series cards
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>
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
 * Driver: ni_pcimio
 * Description: National Instruments PCI-MIO-E series and M series (all boards)
 * Author: ds, John Hallen, Frank Mori Hess, Rolf Mueller, Herbert Peremans,
 *   Herman Bruyninckx, Terry Barnaby
 * Status: works
 * Devices: [National Instruments] PCI-MIO-16XE-50 (ni_pcimio),
 *   PCI-MIO-16XE-10, PXI-6030E, PCI-MIO-16E-1, PCI-MIO-16E-4, PCI-6014,
 *   PCI-6040E, PXI-6040E, PCI-6030E, PCI-6031E, PCI-6032E, PCI-6033E,
 *   PCI-6071E, PCI-6023E, PCI-6024E, PCI-6025E, PXI-6025E, PCI-6034E,
 *   PCI-6035E, PCI-6052E,
 *   PCI-6110, PCI-6111, PCI-6220, PCI-6221, PCI-6224, PXI-6224,
 *   PCI-6225, PXI-6225, PCI-6229, PCI-6250,
 *   PCI-6251, PXI-6251, PCIe-6251, PXIe-6251,
 *   PCI-6254, PCI-6259, PCIe-6259,
 *   PCI-6280, PCI-6281, PXI-6281, PCI-6284, PCI-6289,
 *   PCI-6711, PXI-6711, PCI-6713, PXI-6713,
 *   PXI-6071E, PCI-6070E, PXI-6070E,
 *   PXI-6052E, PCI-6036E, PCI-6731, PCI-6733, PXI-6733,
 *   PCI-6143, PXI-6143
 * Updated: Mon, 09 Jan 2012 14:52:48 +0000
 *
 * These boards are almost identical to the AT-MIO E series, except that
 * they use the PCI bus instead of ISA (i.e., AT). See the notes for the
 * ni_atmio.o driver for additional information about these boards.
 *
 * Autocalibration is supported on many of the devices, using the
 * comedi_calibrate (or comedi_soft_calibrate for m-series) utility.
 * M-Series boards do analog input and analog output calibration entirely
 * in software. The software calibration corrects the analog input for
 * offset, gain and nonlinearity. The analog outputs are corrected for
 * offset and gain. See the comedilib documentation on
 * comedi_get_softcal_converter() for more information.
 *
 * By default, the driver uses DMA to transfer analog input data to
 * memory.  When DMA is enabled, not all triggering features are
 * supported.
 *
 * Digital I/O may not work on 673x.
 *
 * Note that the PCI-6143 is a simultaineous sampling device with 8
 * convertors. With this board all of the convertors perform one
 * simultaineous sample during a scan interval. The period for a scan
 * is used for the convert time in a Comedi cmd. The convert trigger
 * source is normally set to TRIG_NOW by default.
 *
 * The RTSI trigger bus is supported on these cards on subdevice 10.
 * See the comedilib documentation for details.
 *
 * Information (number of channels, bits, etc.) for some devices may be
 * incorrect. Please check this and submit a bug if there are problems
 * for your device.
 *
 * SCXI is probably broken for m-series boards.
 *
 * Bugs:
 * - When DMA is enabled, COMEDI_EV_CONVERT does not work correctly.
 */

/*
 * The PCI-MIO E series driver was originally written by
 * Tomasz Motylewski <...>, and ported to comedi by ds.
 *
 * References:
 *	341079b.pdf  PCI E Series Register-Level Programmer Manual
 *	340934b.pdf  DAQ-STC reference manual
 *
 *	322080b.pdf  6711/6713/6715 User Manual
 *
 *	320945c.pdf  PCI E Series User Manual
 *	322138a.pdf  PCI-6052E and DAQPad-6052E User Manual
 *
 * ISSUES:
 * - need to deal with external reference for DAC, and other DAC
 *   properties in board properties
 * - deal with at-mio-16de-10 revision D to N changes, etc.
 * - need to add other CALDAC type
 * - need to slow down DAC loading. I don't trust NI's claim that
 *   two writes to the PCI bus slows IO enough. I would prefer to
 *   use udelay().
 *   Timing specs: (clock)
 *	AD8522		30ns
 *	DAC8043		120ns
 *	DAC8800		60ns
 *	MB88341		?
 */

#include <linux/module.h>
#include <linux/delay.h>

#include "../comedi_pci.h"

#include <asm/byteorder.h>

#include "ni_stc.h"
#include "mite.h"

#define PCIDMA

/*
 * These are not all the possible ao ranges for 628x boards.
 * They can do OFFSET +- REFERENCE where OFFSET can be
 * 0V, 5V, APFI<0,1>, or AO<0...3> and RANGE can
 * be 10V, 5V, 2V, 1V, APFI<0,1>, AO<0...3>.  That's
 * 63 different possibilities.  An AO channel
 * can not act as it's own OFFSET or REFERENCE.
 */
static const struct comedi_lrange range_ni_M_628x_ao = {
	8, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2),
		BIP_RANGE(1),
		RANGE(-5, 15),
		UNI_RANGE(10),
		RANGE(3, 7),
		RANGE(4, 6),
		RANGE_ext(-1, 1)
	}
};

static const struct comedi_lrange range_ni_M_625x_ao = {
	3, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		RANGE_ext(-1, 1)
	}
};

enum ni_pcimio_boardid {
	BOARD_PCIMIO_16XE_50,
	BOARD_PCIMIO_16XE_10,
	BOARD_PCI6014,
	BOARD_PXI6030E,
	BOARD_PCIMIO_16E_1,
	BOARD_PCIMIO_16E_4,
	BOARD_PXI6040E,
	BOARD_PCI6031E,
	BOARD_PCI6032E,
	BOARD_PCI6033E,
	BOARD_PCI6071E,
	BOARD_PCI6023E,
	BOARD_PCI6024E,
	BOARD_PCI6025E,
	BOARD_PXI6025E,
	BOARD_PCI6034E,
	BOARD_PCI6035E,
	BOARD_PCI6052E,
	BOARD_PCI6110,
	BOARD_PCI6111,
	/* BOARD_PCI6115, */
	/* BOARD_PXI6115, */
	BOARD_PCI6711,
	BOARD_PXI6711,
	BOARD_PCI6713,
	BOARD_PXI6713,
	BOARD_PCI6731,
	/* BOARD_PXI6731, */
	BOARD_PCI6733,
	BOARD_PXI6733,
	BOARD_PXI6071E,
	BOARD_PXI6070E,
	BOARD_PXI6052E,
	BOARD_PXI6031E,
	BOARD_PCI6036E,
	BOARD_PCI6220,
	BOARD_PCI6221,
	BOARD_PCI6221_37PIN,
	BOARD_PCI6224,
	BOARD_PXI6224,
	BOARD_PCI6225,
	BOARD_PXI6225,
	BOARD_PCI6229,
	BOARD_PCI6250,
	BOARD_PCI6251,
	BOARD_PXI6251,
	BOARD_PCIE6251,
	BOARD_PXIE6251,
	BOARD_PCI6254,
	BOARD_PCI6259,
	BOARD_PCIE6259,
	BOARD_PCI6280,
	BOARD_PCI6281,
	BOARD_PXI6281,
	BOARD_PCI6284,
	BOARD_PCI6289,
	BOARD_PCI6143,
	BOARD_PXI6143,
};

static const struct ni_board_struct ni_boards[] = {
	[BOARD_PCIMIO_16XE_50] = {
		.name		= "pci-mio-16xe-50",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 2048,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_8,
		.ai_speed	= 50000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 50000,
		.caldac		= { dac8800, dac8043 },
	},
	[BOARD_PCIMIO_16XE_10] = {
		.name		= "pci-mio-16xe-10",	/*  aka pci-6030E */
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCI6014] = {
		.name		= "pci-6014",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PXI6030E] = {
		.name		= "pxi-6030e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCIMIO_16E_1] = {
		.name		= "pci-mio-16e-1",	/* aka pci-6070e */
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { mb88341 },
	},
	[BOARD_PCIMIO_16E_4] = {
		.name		= "pci-mio-16e-4",	/* aka pci-6040e */
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_16,
		/*
		 * there have been reported problems with
		 * full speed on this board
		 */
		.ai_speed	= 2000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 512,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { ad8804_debug },	/* doc says mb88341 */
	},
	[BOARD_PXI6040E] = {
		.name		= "pxi-6040e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 2000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 512,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { mb88341 },
	},
	[BOARD_PCI6031E] = {
		.name		= "pci-6031e",
		.n_adchan	= 64,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCI6032E] = {
		.name		= "pci-6032e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCI6033E] = {
		.name		= "pci-6033e",
		.n_adchan	= 64,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCI6071E] = {
		.name		= "pci-6071e",
		.n_adchan	= 64,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PCI6023E] = {
		.name		= "pci-6023e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.caldac		= { ad8804_debug },	/* manual is wrong */
	},
	[BOARD_PCI6024E] = {
		.name		= "pci-6024e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },	/* manual is wrong */
	},
	[BOARD_PCI6025E] = {
		.name		= "pci-6025e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },	/* manual is wrong */
		.has_8255	= 1,
	},
	[BOARD_PXI6025E] = {
		.name		= "pxi-6025e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },	/* manual is wrong */
		.has_8255	= 1,
	},
	[BOARD_PCI6034E] = {
		.name		= "pci-6034e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PCI6035E] = {
		.name		= "pci-6035e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PCI6052E] = {
		.name		= "pci-6052e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 3000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 3000,
		/* manual is wrong */
		.caldac		= { ad8804_debug, ad8804_debug, ad8522 },
	},
	[BOARD_PCI6110] = {
		.name		= "pci-6110",
		.n_adchan	= 4,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 8192,
		.alwaysdither	= 0,
		.gainlkup	= ai_gain_611x,
		.ai_speed	= 200,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.reg_type	= ni_reg_611x,
		.ao_range_table	= &range_bipolar10,
		.ao_fifo_depth	= 2048,
		.ao_speed	= 250,
		.caldac		= { ad8804, ad8804 },
	},
	[BOARD_PCI6111] = {
		.name		= "pci-6111",
		.n_adchan	= 2,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 8192,
		.gainlkup	= ai_gain_611x,
		.ai_speed	= 200,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.reg_type	= ni_reg_611x,
		.ao_range_table	= &range_bipolar10,
		.ao_fifo_depth	= 2048,
		.ao_speed	= 250,
		.caldac		= { ad8804, ad8804 },
	},
#if 0
	/* The 6115 boards probably need their own driver */
	[BOARD_PCI6115] = {	/* .device_id = 0x2ed0, */
		.name		= "pci-6115",
		.n_adchan	= 4,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 8192,
		.gainlkup	= ai_gain_611x,
		.ai_speed	= 100,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_671x	= 1,
		.ao_fifo_depth	= 2048,
		.ao_speed	= 250,
		.reg_611x	= 1,
		/* XXX */
		.caldac		= { ad8804_debug, ad8804_debug, ad8804_debug },
	},
#endif
#if 0
	[BOARD_PXI6115] = {	/* .device_id = ????, */
		.name		= "pxi-6115",
		.n_adchan	= 4,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 8192,
		.gainlkup	= ai_gain_611x,
		.ai_speed	= 100,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_671x	= 1,
		.ao_fifo_depth	= 2048,
		.ao_speed	= 250,
		.reg_611x	= 1,
		/* XXX */
		.caldac		= { ad8804_debug, ad8804_debug, ad8804_debug },
	},
#endif
	[BOARD_PCI6711] = {
		.name = "pci-6711",
		.n_aochan	= 4,
		.ao_maxdata	= 0x0fff,
		/* data sheet says 8192, but fifo really holds 16384 samples */
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6711,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PXI6711] = {
		.name		= "pxi-6711",
		.n_aochan	= 4,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6711,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PCI6713] = {
		.name		= "pci-6713",
		.n_aochan	= 8,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6713,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
	[BOARD_PXI6713] = {
		.name		= "pxi-6713",
		.n_aochan	= 8,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6713,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
	[BOARD_PCI6731] = {
		.name		= "pci-6731",
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8192,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6711,
		.caldac		= { ad8804_debug },
	},
#if 0
	[BOARD_PXI6731] = {	/* .device_id = ????, */
		.name		= "pxi-6731",
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8192,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_6711,
		.caldac		= { ad8804_debug },
	},
#endif
	[BOARD_PCI6733] = {
		.name		= "pci-6733",
		.n_aochan	= 8,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6713,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
	[BOARD_PXI6733] = {
		.name		= "pxi-6733",
		.n_aochan	= 8,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 16384,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 1000,
		.reg_type	= ni_reg_6713,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
	[BOARD_PXI6071E] = {
		.name		= "pxi-6071e",
		.n_adchan	= 64,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PXI6070E] = {
		.name		= "pxi-6070e",
		.n_adchan	= 16,
		.ai_maxdata	= 0x0fff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0x0fff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 1000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PXI6052E] = {
		.name		= "pxi-6052e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_16,
		.ai_speed	= 3000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 3000,
		.caldac		= { mb88341, mb88341, ad8522 },
	},
	[BOARD_PXI6031E] = {
		.name		= "pxi-6031e",
		.n_adchan	= 64,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_14,
		.ai_speed	= 10000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 2048,
		.ao_range_table	= &range_ni_E_ao_ext,
		.ao_speed	= 10000,
		.caldac		= { dac8800, dac8043, ad8522 },
	},
	[BOARD_PCI6036E] = {
		.name = "pci-6036e",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,
		.alwaysdither	= 1,
		.gainlkup	= ai_gain_4,
		.ai_speed	= 5000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_range_table	= &range_bipolar10,
		.ao_speed	= 100000,
		.caldac		= { ad8804_debug },
	},
	[BOARD_PCI6220] = {
		.name		= "pci-6220",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 512,		/* FIXME: guess */
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.reg_type	= ni_reg_622x,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6221] = {
		.name		= "pci-6221",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_622x,
		.ao_speed	= 1200,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6221_37PIN] = {
		.name		= "pci-6221_37pin",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_622x,
		.ao_speed	= 1200,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6224] = {
		.name		= "pci-6224",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.reg_type	= ni_reg_622x,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PXI6224] = {
		.name		= "pxi-6224",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.reg_type	= ni_reg_622x,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6225] = {
		.name		= "pci-6225",
		.n_adchan	= 80,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_622x,
		.ao_speed	= 1200,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PXI6225] = {
		.name		= "pxi-6225",
		.n_adchan	= 80,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_622x,
		.ao_speed	= 1200,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6229] = {
		.name		= "pci-6229",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_622x,
		.ai_speed	= 4000,
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_bipolar10,
		.reg_type	= ni_reg_622x,
		.ao_speed	= 1200,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6250] = {
		.name		= "pci-6250",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.reg_type	= ni_reg_625x,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6251] = {
		.name		= "pci-6251",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PXI6251] = {
		.name		= "pxi-6251",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PCIE6251] = {
		.name		= "pcie-6251",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PXIE6251] = {
		.name		= "pxie-6251",
		.n_adchan	= 16,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6254] = {
		.name		= "pci-6254",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.reg_type	= ni_reg_625x,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6259] = {
		.name		= "pci-6259",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCIE6259] = {
		.name		= "pcie-6259",
		.n_adchan	= 32,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 4095,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 800,
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_625x_ao,
		.reg_type	= ni_reg_625x,
		.ao_speed	= 350,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6280] = {
		.name		= "pci-6280",
		.n_adchan	= 16,
		.ai_maxdata	= 0x3ffff,
		.ai_fifo_depth	= 2047,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 1600,
		.ao_fifo_depth	= 8191,
		.reg_type	= ni_reg_628x,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6281] = {
		.name		= "pci-6281",
		.n_adchan	= 16,
		.ai_maxdata	= 0x3ffff,
		.ai_fifo_depth	= 2047,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 1600,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table = &range_ni_M_628x_ao,
		.reg_type	= ni_reg_628x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PXI6281] = {
		.name		= "pxi-6281",
		.n_adchan	= 16,
		.ai_maxdata	= 0x3ffff,
		.ai_fifo_depth	= 2047,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 1600,
		.n_aochan	= 2,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_628x_ao,
		.reg_type	= ni_reg_628x,
		.ao_speed	= 350,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6284] = {
		.name		= "pci-6284",
		.n_adchan	= 32,
		.ai_maxdata	= 0x3ffff,
		.ai_fifo_depth	= 2047,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 1600,
		.reg_type	= ni_reg_628x,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6289] = {
		.name		= "pci-6289",
		.n_adchan	= 32,
		.ai_maxdata	= 0x3ffff,
		.ai_fifo_depth	= 2047,
		.gainlkup	= ai_gain_628x,
		.ai_speed	= 1600,
		.n_aochan	= 4,
		.ao_maxdata	= 0xffff,
		.ao_fifo_depth	= 8191,
		.ao_range_table	= &range_ni_M_628x_ao,
		.reg_type	= ni_reg_628x,
		.ao_speed	= 350,
		.has_32dio_chan	= 1,
		.caldac		= { caldac_none },
	},
	[BOARD_PCI6143] = {
		.name		= "pci-6143",
		.n_adchan	= 8,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_6143,
		.ai_speed	= 4000,
		.reg_type	= ni_reg_6143,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
	[BOARD_PXI6143] = {
		.name		= "pxi-6143",
		.n_adchan	= 8,
		.ai_maxdata	= 0xffff,
		.ai_fifo_depth	= 1024,
		.gainlkup	= ai_gain_6143,
		.ai_speed	= 4000,
		.reg_type	= ni_reg_6143,
		.caldac		= { ad8804_debug, ad8804_debug },
	},
};

#include "ni_mio_common.c"

static int pcimio_ai_change(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->ai_mite_ring, s);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcimio_ao_change(struct comedi_device *dev,
			    struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->ao_mite_ring, s);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcimio_gpct0_change(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->gpct_mite_ring[0], s);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcimio_gpct1_change(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->gpct_mite_ring[1], s);
	if (ret < 0)
		return ret;

	return 0;
}

static int pcimio_dio_change(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct ni_private *devpriv = dev->private;
	int ret;

	ret = mite_buf_change(devpriv->cdo_mite_ring, s);
	if (ret < 0)
		return ret;

	return 0;
}

static void m_series_init_eeprom_buffer(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;
	struct mite *mite = devpriv->mite;
	resource_size_t daq_phys_addr;
	static const int Start_Cal_EEPROM = 0x400;
	static const unsigned int window_size = 10;
	static const int serial_number_eeprom_offset = 0x4;
	static const int serial_number_eeprom_length = 0x4;
	unsigned int old_iodwbsr_bits;
	unsigned int old_iodwbsr1_bits;
	unsigned int old_iodwcr1_bits;
	int i;

	/* IO Window 1 needs to be temporarily mapped to read the eeprom */
	daq_phys_addr = pci_resource_start(mite->pcidev, 1);

	old_iodwbsr_bits = readl(mite->mmio + MITE_IODWBSR);
	old_iodwbsr1_bits = readl(mite->mmio + MITE_IODWBSR_1);
	old_iodwcr1_bits = readl(mite->mmio + MITE_IODWCR_1);
	writel(0x0, mite->mmio + MITE_IODWBSR);
	writel(((0x80 | window_size) | daq_phys_addr),
	       mite->mmio + MITE_IODWBSR_1);
	writel(0x1 | old_iodwcr1_bits, mite->mmio + MITE_IODWCR_1);
	writel(0xf, mite->mmio + 0x30);

	BUG_ON(serial_number_eeprom_length > sizeof(devpriv->serial_number));
	for (i = 0; i < serial_number_eeprom_length; ++i) {
		char *byte_ptr = (char *)&devpriv->serial_number + i;
		*byte_ptr = ni_readb(dev, serial_number_eeprom_offset + i);
	}
	devpriv->serial_number = be32_to_cpu(devpriv->serial_number);

	for (i = 0; i < M_SERIES_EEPROM_SIZE; ++i)
		devpriv->eeprom_buffer[i] = ni_readb(dev, Start_Cal_EEPROM + i);

	writel(old_iodwbsr1_bits, mite->mmio + MITE_IODWBSR_1);
	writel(old_iodwbsr_bits, mite->mmio + MITE_IODWBSR);
	writel(old_iodwcr1_bits, mite->mmio + MITE_IODWCR_1);
	writel(0x0, mite->mmio + 0x30);
}

static void init_6143(struct comedi_device *dev)
{
	const struct ni_board_struct *board = dev->board_ptr;
	struct ni_private *devpriv = dev->private;

	/*  Disable interrupts */
	ni_stc_writew(dev, 0, NISTC_INT_CTRL_REG);

	/*  Initialise 6143 AI specific bits */

	/* Set G0,G1 DMA mode to E series version */
	ni_writeb(dev, 0x00, NI6143_MAGIC_REG);
	/* Set EOCMode, ADCMode and pipelinedelay */
	ni_writeb(dev, 0x80, NI6143_PIPELINE_DELAY_REG);
	/* Set EOC Delay */
	ni_writeb(dev, 0x00, NI6143_EOC_SET_REG);

	/* Set the FIFO half full level */
	ni_writel(dev, board->ai_fifo_depth / 2, NI6143_AI_FIFO_FLAG_REG);

	/*  Strobe Relay disable bit */
	devpriv->ai_calib_source_enabled = 0;
	ni_writew(dev, devpriv->ai_calib_source | NI6143_CALIB_CHAN_RELAY_OFF,
		  NI6143_CALIB_CHAN_REG);
	ni_writew(dev, devpriv->ai_calib_source, NI6143_CALIB_CHAN_REG);
}

static void pcimio_detach(struct comedi_device *dev)
{
	struct ni_private *devpriv = dev->private;

	mio_common_detach(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (devpriv) {
		mite_free_ring(devpriv->ai_mite_ring);
		mite_free_ring(devpriv->ao_mite_ring);
		mite_free_ring(devpriv->cdo_mite_ring);
		mite_free_ring(devpriv->gpct_mite_ring[0]);
		mite_free_ring(devpriv->gpct_mite_ring[1]);
		mite_detach(devpriv->mite);
	}
	if (dev->mmio)
		iounmap(dev->mmio);
	comedi_pci_disable(dev);
}

static int pcimio_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pci_dev *pcidev = comedi_to_pci_dev(dev);
	const struct ni_board_struct *board = NULL;
	struct ni_private *devpriv;
	unsigned int irq;
	int ret;

	if (context < ARRAY_SIZE(ni_boards))
		board = &ni_boards[context];
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	ret = comedi_pci_enable(dev);
	if (ret)
		return ret;

	ret = ni_alloc_private(dev);
	if (ret)
		return ret;
	devpriv = dev->private;

	devpriv->mite = mite_attach(dev, false);	/* use win0 */
	if (!devpriv->mite)
		return -ENOMEM;

	if (board->reg_type & ni_reg_m_series_mask)
		devpriv->is_m_series = 1;
	if (board->reg_type & ni_reg_6xxx_mask)
		devpriv->is_6xxx = 1;
	if (board->reg_type == ni_reg_611x)
		devpriv->is_611x = 1;
	if (board->reg_type == ni_reg_6143)
		devpriv->is_6143 = 1;
	if (board->reg_type == ni_reg_622x)
		devpriv->is_622x = 1;
	if (board->reg_type == ni_reg_625x)
		devpriv->is_625x = 1;
	if (board->reg_type == ni_reg_628x)
		devpriv->is_628x = 1;
	if (board->reg_type & ni_reg_67xx_mask)
		devpriv->is_67xx = 1;
	if (board->reg_type == ni_reg_6711)
		devpriv->is_6711 = 1;
	if (board->reg_type == ni_reg_6713)
		devpriv->is_6713 = 1;

	devpriv->ai_mite_ring = mite_alloc_ring(devpriv->mite);
	if (!devpriv->ai_mite_ring)
		return -ENOMEM;
	devpriv->ao_mite_ring = mite_alloc_ring(devpriv->mite);
	if (!devpriv->ao_mite_ring)
		return -ENOMEM;
	devpriv->cdo_mite_ring = mite_alloc_ring(devpriv->mite);
	if (!devpriv->cdo_mite_ring)
		return -ENOMEM;
	devpriv->gpct_mite_ring[0] = mite_alloc_ring(devpriv->mite);
	if (!devpriv->gpct_mite_ring[0])
		return -ENOMEM;
	devpriv->gpct_mite_ring[1] = mite_alloc_ring(devpriv->mite);
	if (!devpriv->gpct_mite_ring[1])
		return -ENOMEM;

	if (devpriv->is_m_series)
		m_series_init_eeprom_buffer(dev);
	if (devpriv->is_6143)
		init_6143(dev);

	irq = pcidev->irq;
	if (irq) {
		ret = request_irq(irq, ni_E_interrupt, IRQF_SHARED,
				  dev->board_name, dev);
		if (ret == 0)
			dev->irq = irq;
	}

	ret = ni_E_init(dev, 0, 1);
	if (ret < 0)
		return ret;

	dev->subdevices[NI_AI_SUBDEV].buf_change = &pcimio_ai_change;
	dev->subdevices[NI_AO_SUBDEV].buf_change = &pcimio_ao_change;
	dev->subdevices[NI_GPCT_SUBDEV(0)].buf_change = &pcimio_gpct0_change;
	dev->subdevices[NI_GPCT_SUBDEV(1)].buf_change = &pcimio_gpct1_change;
	dev->subdevices[NI_DIO_SUBDEV].buf_change = &pcimio_dio_change;

	return 0;
}

static struct comedi_driver ni_pcimio_driver = {
	.driver_name	= "ni_pcimio",
	.module		= THIS_MODULE,
	.auto_attach	= pcimio_auto_attach,
	.detach		= pcimio_detach,
};

static int ni_pcimio_pci_probe(struct pci_dev *dev,
			       const struct pci_device_id *id)
{
	return comedi_pci_auto_config(dev, &ni_pcimio_driver, id->driver_data);
}

static const struct pci_device_id ni_pcimio_pci_table[] = {
	{ PCI_VDEVICE(NI, 0x0162), BOARD_PCIMIO_16XE_50 },	/* 0x1620? */
	{ PCI_VDEVICE(NI, 0x1170), BOARD_PCIMIO_16XE_10 },
	{ PCI_VDEVICE(NI, 0x1180), BOARD_PCIMIO_16E_1 },
	{ PCI_VDEVICE(NI, 0x1190), BOARD_PCIMIO_16E_4 },
	{ PCI_VDEVICE(NI, 0x11b0), BOARD_PXI6070E },
	{ PCI_VDEVICE(NI, 0x11c0), BOARD_PXI6040E },
	{ PCI_VDEVICE(NI, 0x11d0), BOARD_PXI6030E },
	{ PCI_VDEVICE(NI, 0x1270), BOARD_PCI6032E },
	{ PCI_VDEVICE(NI, 0x1330), BOARD_PCI6031E },
	{ PCI_VDEVICE(NI, 0x1340), BOARD_PCI6033E },
	{ PCI_VDEVICE(NI, 0x1350), BOARD_PCI6071E },
	{ PCI_VDEVICE(NI, 0x14e0), BOARD_PCI6110 },
	{ PCI_VDEVICE(NI, 0x14f0), BOARD_PCI6111 },
	{ PCI_VDEVICE(NI, 0x1580), BOARD_PXI6031E },
	{ PCI_VDEVICE(NI, 0x15b0), BOARD_PXI6071E },
	{ PCI_VDEVICE(NI, 0x1880), BOARD_PCI6711 },
	{ PCI_VDEVICE(NI, 0x1870), BOARD_PCI6713 },
	{ PCI_VDEVICE(NI, 0x18b0), BOARD_PCI6052E },
	{ PCI_VDEVICE(NI, 0x18c0), BOARD_PXI6052E },
	{ PCI_VDEVICE(NI, 0x2410), BOARD_PCI6733 },
	{ PCI_VDEVICE(NI, 0x2420), BOARD_PXI6733 },
	{ PCI_VDEVICE(NI, 0x2430), BOARD_PCI6731 },
	{ PCI_VDEVICE(NI, 0x2890), BOARD_PCI6036E },
	{ PCI_VDEVICE(NI, 0x28c0), BOARD_PCI6014 },
	{ PCI_VDEVICE(NI, 0x2a60), BOARD_PCI6023E },
	{ PCI_VDEVICE(NI, 0x2a70), BOARD_PCI6024E },
	{ PCI_VDEVICE(NI, 0x2a80), BOARD_PCI6025E },
	{ PCI_VDEVICE(NI, 0x2ab0), BOARD_PXI6025E },
	{ PCI_VDEVICE(NI, 0x2b80), BOARD_PXI6713 },
	{ PCI_VDEVICE(NI, 0x2b90), BOARD_PXI6711 },
	{ PCI_VDEVICE(NI, 0x2c80), BOARD_PCI6035E },
	{ PCI_VDEVICE(NI, 0x2ca0), BOARD_PCI6034E },
	{ PCI_VDEVICE(NI, 0x70aa), BOARD_PCI6229 },
	{ PCI_VDEVICE(NI, 0x70ab), BOARD_PCI6259 },
	{ PCI_VDEVICE(NI, 0x70ac), BOARD_PCI6289 },
	{ PCI_VDEVICE(NI, 0x70af), BOARD_PCI6221 },
	{ PCI_VDEVICE(NI, 0x70b0), BOARD_PCI6220 },
	{ PCI_VDEVICE(NI, 0x70b4), BOARD_PCI6250 },
	{ PCI_VDEVICE(NI, 0x70b6), BOARD_PCI6280 },
	{ PCI_VDEVICE(NI, 0x70b7), BOARD_PCI6254 },
	{ PCI_VDEVICE(NI, 0x70b8), BOARD_PCI6251 },
	{ PCI_VDEVICE(NI, 0x70bc), BOARD_PCI6284 },
	{ PCI_VDEVICE(NI, 0x70bd), BOARD_PCI6281 },
	{ PCI_VDEVICE(NI, 0x70bf), BOARD_PXI6281 },
	{ PCI_VDEVICE(NI, 0x70c0), BOARD_PCI6143 },
	{ PCI_VDEVICE(NI, 0x70f2), BOARD_PCI6224 },
	{ PCI_VDEVICE(NI, 0x70f3), BOARD_PXI6224 },
	{ PCI_VDEVICE(NI, 0x710d), BOARD_PXI6143 },
	{ PCI_VDEVICE(NI, 0x716c), BOARD_PCI6225 },
	{ PCI_VDEVICE(NI, 0x716d), BOARD_PXI6225 },
	{ PCI_VDEVICE(NI, 0x717f), BOARD_PCIE6259 },
	{ PCI_VDEVICE(NI, 0x71bc), BOARD_PCI6221_37PIN },
	{ PCI_VDEVICE(NI, 0x717d), BOARD_PCIE6251 },
	{ PCI_VDEVICE(NI, 0x72e8), BOARD_PXIE6251 },
	{ PCI_VDEVICE(NI, 0x70ad), BOARD_PXI6251 },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ni_pcimio_pci_table);

static struct pci_driver ni_pcimio_pci_driver = {
	.name		= "ni_pcimio",
	.id_table	= ni_pcimio_pci_table,
	.probe		= ni_pcimio_pci_probe,
	.remove		= comedi_pci_auto_unconfig,
};
module_comedi_pci_driver(ni_pcimio_driver, ni_pcimio_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
