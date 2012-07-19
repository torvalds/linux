/*
    comedi/drivers/amplc_dio200.c
    Driver for Amplicon PC272E and PCI272 DIO boards.
    (Support for other boards in Amplicon 200 series may be added at
    a later date, e.g. PCI215.)

    Copyright (C) 2005 MEV Ltd. <http://www.mev.co.uk/>

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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: amplc_dio200
Description: Amplicon 200 Series Digital I/O
Author: Ian Abbott <abbotti@mev.co.uk>
Devices: [Amplicon] PC212E (pc212e), PC214E (pc214e), PC215E (pc215e),
  PCI215 (pci215 or amplc_dio200), PC218E (pc218e), PC272E (pc272e),
  PCI272 (pci272 or amplc_dio200)
Updated: Wed, 22 Oct 2008 13:36:02 +0100
Status: works

Configuration options - PC212E, PC214E, PC215E, PC218E, PC272E:
  [0] - I/O port base address
  [1] - IRQ (optional, but commands won't work without it)

Configuration options - PCI215, PCI272:
  [0] - PCI bus of device (optional)
  [1] - PCI slot of device (optional)
  If bus/slot is not specified, the first available PCI device will
  be used.

Passing a zero for an option is the same as leaving it unspecified.

SUBDEVICES

		    PC218E         PC212E      PC215E/PCI215
		 -------------  -------------  -------------
  Subdevices           7              6              5
   0                 CTR-X1         PPI-X          PPI-X
   1                 CTR-X2         CTR-Y1         PPI-Y
   2                 CTR-Y1         CTR-Y2         CTR-Z1
   3                 CTR-Y2         CTR-Z1         CTR-Z2
   4                 CTR-Z1         CTR-Z2       INTERRUPT
   5                 CTR-Z2       INTERRUPT
   6               INTERRUPT

		    PC214E      PC272E/PCI272
		 -------------  -------------
  Subdevices           4              4
   0                 PPI-X          PPI-X
   1                 PPI-Y          PPI-Y
   2                 CTR-Z1*        PPI-Z
   3               INTERRUPT*     INTERRUPT

Each PPI is a 8255 chip providing 24 DIO channels.  The DIO channels
are configurable as inputs or outputs in four groups:

  Port A  - channels  0 to  7
  Port B  - channels  8 to 15
  Port CL - channels 16 to 19
  Port CH - channels 20 to 23

Only mode 0 of the 8255 chips is supported.

Each CTR is a 8254 chip providing 3 16-bit counter channels.  Each
channel is configured individually with INSN_CONFIG instructions.  The
specific type of configuration instruction is specified in data[0].
Some configuration instructions expect an additional parameter in
data[1]; others return a value in data[1].  The following configuration
instructions are supported:

  INSN_CONFIG_SET_COUNTER_MODE.  Sets the counter channel's mode and
    BCD/binary setting specified in data[1].

  INSN_CONFIG_8254_READ_STATUS.  Reads the status register value for the
    counter channel into data[1].

  INSN_CONFIG_SET_CLOCK_SRC.  Sets the counter channel's clock source as
    specified in data[1] (this is a hardware-specific value).  Not
    supported on PC214E.  For the other boards, valid clock sources are
    0 to 7 as follows:

      0.  CLK n, the counter channel's dedicated CLK input from the SK1
	connector.  (N.B. for other values, the counter channel's CLKn
	pin on the SK1 connector is an output!)
      1.  Internal 10 MHz clock.
      2.  Internal 1 MHz clock.
      3.  Internal 100 kHz clock.
      4.  Internal 10 kHz clock.
      5.  Internal 1 kHz clock.
      6.  OUT n-1, the output of counter channel n-1 (see note 1 below).
      7.  Ext Clock, the counter chip's dedicated Ext Clock input from
	the SK1 connector.  This pin is shared by all three counter
	channels on the chip.

  INSN_CONFIG_GET_CLOCK_SRC.  Returns the counter channel's current
    clock source in data[1].  For internal clock sources, data[2] is set
    to the period in ns.

  INSN_CONFIG_SET_GATE_SRC.  Sets the counter channel's gate source as
    specified in data[2] (this is a hardware-specific value).  Not
    supported on PC214E.  For the other boards, valid gate sources are 0
    to 7 as follows:

      0.  VCC (internal +5V d.c.), i.e. gate permanently enabled.
      1.  GND (internal 0V d.c.), i.e. gate permanently disabled.
      2.  GAT n, the counter channel's dedicated GAT input from the SK1
	connector.  (N.B. for other values, the counter channel's GATn
	pin on the SK1 connector is an output!)
      3.  /OUT n-2, the inverted output of counter channel n-2 (see note
	2 below).
      4.  Reserved.
      5.  Reserved.
      6.  Reserved.
      7.  Reserved.

  INSN_CONFIG_GET_GATE_SRC.  Returns the counter channel's current gate
    source in data[2].

Clock and gate interconnection notes:

  1.  Clock source OUT n-1 is the output of the preceding channel on the
  same counter subdevice if n > 0, or the output of channel 2 on the
  preceding counter subdevice (see note 3) if n = 0.

  2.  Gate source /OUT n-2 is the inverted output of channel 0 on the
  same counter subdevice if n = 2, or the inverted output of channel n+1
  on the preceding counter subdevice (see note 3) if n < 2.

  3.  The counter subdevices are connected in a ring, so the highest
  counter subdevice precedes the lowest.

The 'INTERRUPT' subdevice pretends to be a digital input subdevice.  The
digital inputs come from the interrupt status register.  The number of
channels matches the number of interrupt sources.  The PC214E does not
have an interrupt status register; see notes on 'INTERRUPT SOURCES'
below.

INTERRUPT SOURCES

		    PC218E         PC212E      PC215E/PCI215
		 -------------  -------------  -------------
  Sources              6              6              6
   0              CTR-X1-OUT      PPI-X-C0       PPI-X-C0
   1              CTR-X2-OUT      PPI-X-C3       PPI-X-C3
   2              CTR-Y1-OUT     CTR-Y1-OUT      PPI-Y-C0
   3              CTR-Y2-OUT     CTR-Y2-OUT      PPI-Y-C3
   4              CTR-Z1-OUT     CTR-Z1-OUT     CTR-Z1-OUT
   5              CTR-Z2-OUT     CTR-Z2-OUT     CTR-Z2-OUT

		    PC214E      PC272E/PCI272
		 -------------  -------------
  Sources              1              6
   0               JUMPER-J5      PPI-X-C0
   1                              PPI-X-C3
   2                              PPI-Y-C0
   3                              PPI-Y-C3
   4                              PPI-Z-C0
   5                              PPI-Z-C3

When an interrupt source is enabled in the interrupt source enable
register, a rising edge on the source signal latches the corresponding
bit to 1 in the interrupt status register.

When the interrupt status register value as a whole (actually, just the
6 least significant bits) goes from zero to non-zero, the board will
generate an interrupt.  For level-triggered hardware interrupts (PCI
card), the interrupt will remain asserted until the interrupt status
register is cleared to zero.  For edge-triggered hardware interrupts
(ISA card), no further interrupts will occur until the interrupt status
register is cleared to zero.  To clear a bit to zero in the interrupt
status register, the corresponding interrupt source must be disabled
in the interrupt source enable register (there is no separate interrupt
clear register).

The PC214E does not have an interrupt source enable register or an
interrupt status register; its 'INTERRUPT' subdevice has a single
channel and its interrupt source is selected by the position of jumper
J5.

COMMANDS

The driver supports a read streaming acquisition command on the
'INTERRUPT' subdevice.  The channel list selects the interrupt sources
to be enabled.  All channels will be sampled together (convert_src ==
TRIG_NOW).  The scan begins a short time after the hardware interrupt
occurs, subject to interrupt latencies (scan_begin_src == TRIG_EXT,
scan_begin_arg == 0).  The value read from the interrupt status register
is packed into a short value, one bit per requested channel, in the
order they appear in the channel list.
*/

#include <linux/interrupt.h>
#include <linux/slab.h>

#include "../comedidev.h"

#include "8255.h"
#include "8253.h"

#define DIO200_DRIVER_NAME	"amplc_dio200"

/* PCI IDs */
#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI_DEVICE_ID_AMPLICON_PCI272 0x000a
#define PCI_DEVICE_ID_AMPLICON_PCI215 0x000b
#define PCI_DEVICE_ID_INVALID 0xffff

/* 200 series registers */
#define DIO200_IO_SIZE		0x20
#define DIO200_XCLK_SCE		0x18	/* Group X clock selection register */
#define DIO200_YCLK_SCE		0x19	/* Group Y clock selection register */
#define DIO200_ZCLK_SCE		0x1a	/* Group Z clock selection register */
#define DIO200_XGAT_SCE		0x1b	/* Group X gate selection register */
#define DIO200_YGAT_SCE		0x1c	/* Group Y gate selection register */
#define DIO200_ZGAT_SCE		0x1d	/* Group Z gate selection register */
#define DIO200_INT_SCE		0x1e	/* Interrupt enable/status register */

/*
 * Macros for constructing value for DIO_200_?CLK_SCE and
 * DIO_200_?GAT_SCE registers:
 *
 * 'which' is: 0 for CTR-X1, CTR-Y1, CTR-Z1; 1 for CTR-X2, CTR-Y2 or CTR-Z2.
 * 'chan' is the channel: 0, 1 or 2.
 * 'source' is the signal source: 0 to 7.
 */
#define CLK_SCE(which, chan, source) (((which) << 5) | ((chan) << 3) | (source))
#define GAT_SCE(which, chan, source) (((which) << 5) | ((chan) << 3) | (source))

/*
 * Periods of the internal clock sources in nanoseconds.
 */
static const unsigned clock_period[8] = {
	0,			/* dedicated clock input/output pin */
	100,			/* 10 MHz */
	1000,			/* 1 MHz */
	10000,			/* 100 kHz */
	100000,			/* 10 kHz */
	1000000,		/* 1 kHz */
	0,			/* OUT N-1 */
	0			/* group clock input pin */
};

/*
 * Board descriptions.
 */

enum dio200_bustype { isa_bustype, pci_bustype };

enum dio200_model {
	pc212e_model,
	pc214e_model,
	pc215e_model, pci215_model,
	pc218e_model,
	pc272e_model, pci272_model,
	anypci_model
};

enum dio200_layout {
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA)
	pc212_layout,
	pc214_layout,
#endif
	pc215_layout,
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA)
	pc218_layout,
#endif
	pc272_layout
};

struct dio200_board {
	const char *name;
	unsigned short devid;
	enum dio200_bustype bustype;
	enum dio200_model model;
	enum dio200_layout layout;
};

static const struct dio200_board dio200_boards[] = {
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA)
	{
	 .name = "pc212e",
	 .bustype = isa_bustype,
	 .model = pc212e_model,
	 .layout = pc212_layout,
	 },
	{
	 .name = "pc214e",
	 .bustype = isa_bustype,
	 .model = pc214e_model,
	 .layout = pc214_layout,
	 },
	{
	 .name = "pc215e",
	 .bustype = isa_bustype,
	 .model = pc215e_model,
	 .layout = pc215_layout,
	 },
	{
	 .name = "pc218e",
	 .bustype = isa_bustype,
	 .model = pc218e_model,
	 .layout = pc218_layout,
	 },
	{
	 .name = "pc272e",
	 .bustype = isa_bustype,
	 .model = pc272e_model,
	 .layout = pc272_layout,
	 },
#endif
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI)
	{
	 .name = "pci215",
	 .devid = PCI_DEVICE_ID_AMPLICON_PCI215,
	 .bustype = pci_bustype,
	 .model = pci215_model,
	 .layout = pc215_layout,
	 },
	{
	 .name = "pci272",
	 .devid = PCI_DEVICE_ID_AMPLICON_PCI272,
	 .bustype = pci_bustype,
	 .model = pci272_model,
	 .layout = pc272_layout,
	 },
	{
	 .name = DIO200_DRIVER_NAME,
	 .devid = PCI_DEVICE_ID_INVALID,
	 .bustype = pci_bustype,
	 .model = anypci_model,	/* wildcard */
	 },
#endif
};

/*
 * Layout descriptions - some ISA and PCI board descriptions share the same
 * layout.
 */

enum dio200_sdtype { sd_none, sd_intr, sd_8255, sd_8254 };

#define DIO200_MAX_SUBDEVS	7
#define DIO200_MAX_ISNS		6

struct dio200_layout_struct {
	unsigned short n_subdevs;	/* number of subdevices */
	unsigned char sdtype[DIO200_MAX_SUBDEVS];	/* enum dio200_sdtype */
	unsigned char sdinfo[DIO200_MAX_SUBDEVS];	/* depends on sdtype */
	char has_int_sce;	/* has interrupt enable/status register */
	char has_clk_gat_sce;	/* has clock/gate selection registers */
};

static const struct dio200_layout_struct dio200_layouts[] = {
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA)
	[pc212_layout] = {
			  .n_subdevs = 6,
			  .sdtype = {sd_8255, sd_8254, sd_8254, sd_8254,
				     sd_8254,
				     sd_intr},
			  .sdinfo = {0x00, 0x08, 0x0C, 0x10, 0x14,
				     0x3F},
			  .has_int_sce = 1,
			  .has_clk_gat_sce = 1,
			  },
	[pc214_layout] = {
			  .n_subdevs = 4,
			  .sdtype = {sd_8255, sd_8255, sd_8254,
				     sd_intr},
			  .sdinfo = {0x00, 0x08, 0x10, 0x01},
			  .has_int_sce = 0,
			  .has_clk_gat_sce = 0,
			  },
#endif
	[pc215_layout] = {
			  .n_subdevs = 5,
			  .sdtype = {sd_8255, sd_8255, sd_8254,
				     sd_8254,
				     sd_intr},
			  .sdinfo = {0x00, 0x08, 0x10, 0x14, 0x3F},
			  .has_int_sce = 1,
			  .has_clk_gat_sce = 1,
			  },
#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA)
	[pc218_layout] = {
			  .n_subdevs = 7,
			  .sdtype = {sd_8254, sd_8254, sd_8255, sd_8254,
				     sd_8254,
				     sd_intr},
			  .sdinfo = {0x00, 0x04, 0x08, 0x0C, 0x10,
				     0x14,
				     0x3F},
			  .has_int_sce = 1,
			  .has_clk_gat_sce = 1,
			  },
#endif
	[pc272_layout] = {
			  .n_subdevs = 4,
			  .sdtype = {sd_8255, sd_8255, sd_8255,
				     sd_intr},
			  .sdinfo = {0x00, 0x08, 0x10, 0x3F},
			  .has_int_sce = 1,
			  .has_clk_gat_sce = 0,
			  },
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.
 */
struct dio200_private {
	struct pci_dev *pci_dev;	/* PCI device */
	int intr_sd;
};

struct dio200_subdev_8254 {
	unsigned long iobase;	/* Counter base address */
	unsigned long clk_sce_iobase;	/* CLK_SCE base address */
	unsigned long gat_sce_iobase;	/* GAT_SCE base address */
	int which;		/* Bit 5 of CLK_SCE or GAT_SCE */
	int has_clk_gat_sce;
	unsigned clock_src[3];	/* Current clock sources */
	unsigned gate_src[3];	/* Current gate sources */
	spinlock_t spinlock;
};

struct dio200_subdev_intr {
	unsigned long iobase;
	spinlock_t spinlock;
	int active;
	int has_int_sce;
	unsigned int valid_isns;
	unsigned int enabled_isns;
	unsigned int stopcount;
	int continuous;
};

/*
 * This function looks for a board matching the supplied PCI device.
 */
static const struct dio200_board *
dio200_find_pci_board(struct pci_dev *pci_dev)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(dio200_boards); i++)
		if (dio200_boards[i].bustype == pci_bustype &&
		    pci_dev->device == dio200_boards[i].devid)
			return &dio200_boards[i];
	return NULL;
}

/*
 * This function looks for a PCI device matching the requested board name,
 * bus and slot.
 */
static struct pci_dev *dio200_find_pci_dev(struct comedi_device *dev,
					   struct comedi_devconfig *it)
{
	const struct dio200_board *thisboard = comedi_board(dev);
	struct pci_dev *pci_dev = NULL;
	int bus = it->options[0];
	int slot = it->options[1];

	for_each_pci_dev(pci_dev) {
		if (bus || slot) {
			if (bus != pci_dev->bus->number ||
			    slot != PCI_SLOT(pci_dev->devfn))
				continue;
		}
		if (pci_dev->vendor != PCI_VENDOR_ID_AMPLICON)
			continue;

		if (thisboard->model == anypci_model) {
			/* Wildcard board matches any supported PCI board. */
			const struct dio200_board *foundboard;

			foundboard = dio200_find_pci_board(pci_dev);
			if (foundboard == NULL)
				continue;
			/* Replace wildcard board_ptr. */
			dev->board_ptr = foundboard;
		} else {
			/* Match specific model name. */
			if (pci_dev->device != thisboard->devid)
				continue;
		}
		return pci_dev;
	}
	dev_err(dev->class_dev,
		"No supported board found! (req. bus %d, slot %d)\n",
		bus, slot);
	return NULL;
}

/*
 * This function checks and requests an I/O region, reporting an error
 * if there is a conflict.
 */
static int
dio200_request_region(struct comedi_device *dev,
		      unsigned long from, unsigned long extent)
{
	if (!from || !request_region(from, extent, DIO200_DRIVER_NAME)) {
		dev_err(dev->class_dev, "I/O port conflict (%#lx,%lu)!\n",
			from, extent);
		return -EIO;
	}
	return 0;
}

/*
 * 'insn_bits' function for an 'INTERRUPT' subdevice.
 */
static int
dio200_subdev_intr_insn_bits(struct comedi_device *dev,
			     struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	struct dio200_subdev_intr *subpriv = s->private;

	if (subpriv->has_int_sce) {
		/* Just read the interrupt status register.  */
		data[1] = inb(subpriv->iobase) & subpriv->valid_isns;
	} else {
		/* No interrupt status register. */
		data[0] = 0;
	}

	return insn->n;
}

/*
 * Called to stop acquisition for an 'INTERRUPT' subdevice.
 */
static void dio200_stop_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;

	subpriv->active = 0;
	subpriv->enabled_isns = 0;
	if (subpriv->has_int_sce)
		outb(0, subpriv->iobase);
}

/*
 * Called to start acquisition for an 'INTERRUPT' subdevice.
 */
static int dio200_start_intr(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	unsigned int n;
	unsigned isn_bits;
	struct dio200_subdev_intr *subpriv = s->private;
	struct comedi_cmd *cmd = &s->async->cmd;
	int retval = 0;

	if (!subpriv->continuous && subpriv->stopcount == 0) {
		/* An empty acquisition! */
		s->async->events |= COMEDI_CB_EOA;
		subpriv->active = 0;
		retval = 1;
	} else {
		/* Determine interrupt sources to enable. */
		isn_bits = 0;
		if (cmd->chanlist) {
			for (n = 0; n < cmd->chanlist_len; n++)
				isn_bits |= (1U << CR_CHAN(cmd->chanlist[n]));
		}
		isn_bits &= subpriv->valid_isns;
		/* Enable interrupt sources. */
		subpriv->enabled_isns = isn_bits;
		if (subpriv->has_int_sce)
			outb(isn_bits, subpriv->iobase);
	}

	return retval;
}

/*
 * Internal trigger function to start acquisition for an 'INTERRUPT' subdevice.
 */
static int
dio200_inttrig_start_intr(struct comedi_device *dev, struct comedi_subdevice *s,
			  unsigned int trignum)
{
	struct dio200_subdev_intr *subpriv;
	unsigned long flags;
	int event = 0;

	if (trignum != 0)
		return -EINVAL;

	subpriv = s->private;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	s->async->inttrig = NULL;
	if (subpriv->active)
		event = dio200_start_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 1;
}

/*
 * This is called from the interrupt service routine to handle a read
 * scan on an 'INTERRUPT' subdevice.
 */
static int dio200_handle_read_intr(struct comedi_device *dev,
				   struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned triggered;
	unsigned intstat;
	unsigned cur_enabled;
	unsigned int oldevents;
	unsigned long flags;

	triggered = 0;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	oldevents = s->async->events;
	if (subpriv->has_int_sce) {
		/*
		 * Collect interrupt sources that have triggered and disable
		 * them temporarily.  Loop around until no extra interrupt
		 * sources have triggered, at which point, the valid part of
		 * the interrupt status register will read zero, clearing the
		 * cause of the interrupt.
		 *
		 * Mask off interrupt sources already seen to avoid infinite
		 * loop in case of misconfiguration.
		 */
		cur_enabled = subpriv->enabled_isns;
		while ((intstat = (inb(subpriv->iobase) & subpriv->valid_isns
				   & ~triggered)) != 0) {
			triggered |= intstat;
			cur_enabled &= ~triggered;
			outb(cur_enabled, subpriv->iobase);
		}
	} else {
		/*
		 * No interrupt status register.  Assume the single interrupt
		 * source has triggered.
		 */
		triggered = subpriv->enabled_isns;
	}

	if (triggered) {
		/*
		 * Some interrupt sources have triggered and have been
		 * temporarily disabled to clear the cause of the interrupt.
		 *
		 * Reenable them NOW to minimize the time they are disabled.
		 */
		cur_enabled = subpriv->enabled_isns;
		if (subpriv->has_int_sce)
			outb(cur_enabled, subpriv->iobase);

		if (subpriv->active) {
			/*
			 * The command is still active.
			 *
			 * Ignore interrupt sources that the command isn't
			 * interested in (just in case there's a race
			 * condition).
			 */
			if (triggered & subpriv->enabled_isns) {
				/* Collect scan data. */
				short val;
				unsigned int n, ch, len;

				val = 0;
				len = s->async->cmd.chanlist_len;
				for (n = 0; n < len; n++) {
					ch = CR_CHAN(s->async->cmd.chanlist[n]);
					if (triggered & (1U << ch))
						val |= (1U << n);
				}
				/* Write the scan to the buffer. */
				if (comedi_buf_put(s->async, val)) {
					s->async->events |= (COMEDI_CB_BLOCK |
							     COMEDI_CB_EOS);
				} else {
					/* Error!  Stop acquisition.  */
					dio200_stop_intr(dev, s);
					s->async->events |= COMEDI_CB_ERROR
					    | COMEDI_CB_OVERFLOW;
					comedi_error(dev, "buffer overflow");
				}

				/* Check for end of acquisition. */
				if (!subpriv->continuous) {
					/* stop_src == TRIG_COUNT */
					if (subpriv->stopcount > 0) {
						subpriv->stopcount--;
						if (subpriv->stopcount == 0) {
							s->async->events |=
							    COMEDI_CB_EOA;
							dio200_stop_intr(dev,
									 s);
						}
					}
				}
			}
		}
	}
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	if (oldevents != s->async->events)
		comedi_event(dev, s);

	return (triggered != 0);
}

/*
 * 'cancel' function for an 'INTERRUPT' subdevice.
 */
static int dio200_subdev_intr_cancel(struct comedi_device *dev,
				     struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	if (subpriv->active)
		dio200_stop_intr(dev, s);

	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 0;
}

/*
 * 'do_cmdtest' function for an 'INTERRUPT' subdevice.
 */
static int
dio200_subdev_intr_cmdtest(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int tmp;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= (TRIG_NOW | TRIG_INT);
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_NOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= (TRIG_COUNT | TRIG_NONE);
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and mutually
		   compatible */

	/* these tests are true if more than one _src bit is set */
	if ((cmd->start_src & (cmd->start_src - 1)) != 0)
		err++;
	if ((cmd->scan_begin_src & (cmd->scan_begin_src - 1)) != 0)
		err++;
	if ((cmd->convert_src & (cmd->convert_src - 1)) != 0)
		err++;
	if ((cmd->scan_end_src & (cmd->scan_end_src - 1)) != 0)
		err++;
	if ((cmd->stop_src & (cmd->stop_src - 1)) != 0)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	/* cmd->start_src == TRIG_NOW || cmd->start_src == TRIG_INT */
	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}

	/* cmd->scan_begin_src == TRIG_EXT */
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}

	/* cmd->convert_src == TRIG_NOW */
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}

	/* cmd->scan_end_src == TRIG_COUNT */
	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}

	switch (cmd->stop_src) {
	case TRIG_COUNT:
		/* any count allowed */
		break;
	case TRIG_NONE:
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
		break;
	default:
		break;
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	/* if (err) return 4; */

	return 0;
}

/*
 * 'do_cmd' function for an 'INTERRUPT' subdevice.
 */
static int dio200_subdev_intr_cmd(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct comedi_cmd *cmd = &s->async->cmd;
	struct dio200_subdev_intr *subpriv = s->private;
	unsigned long flags;
	int event = 0;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	subpriv->active = 1;

	/* Set up end of acquisition. */
	switch (cmd->stop_src) {
	case TRIG_COUNT:
		subpriv->continuous = 0;
		subpriv->stopcount = cmd->stop_arg;
		break;
	default:
		/* TRIG_NONE */
		subpriv->continuous = 1;
		subpriv->stopcount = 0;
		break;
	}

	/* Set up start of acquisition. */
	switch (cmd->start_src) {
	case TRIG_INT:
		s->async->inttrig = dio200_inttrig_start_intr;
		break;
	default:
		/* TRIG_NOW */
		event = dio200_start_intr(dev, s);
		break;
	}
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	if (event)
		comedi_event(dev, s);

	return 0;
}

/*
 * This function initializes an 'INTERRUPT' subdevice.
 */
static int
dio200_subdev_intr_init(struct comedi_device *dev, struct comedi_subdevice *s,
			unsigned long iobase, unsigned valid_isns,
			int has_int_sce)
{
	struct dio200_subdev_intr *subpriv;

	subpriv = kzalloc(sizeof(*subpriv), GFP_KERNEL);
	if (!subpriv) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return -ENOMEM;
	}
	subpriv->iobase = iobase;
	subpriv->has_int_sce = has_int_sce;
	subpriv->valid_isns = valid_isns;
	spin_lock_init(&subpriv->spinlock);

	if (has_int_sce)
		outb(0, subpriv->iobase);	/* Disable interrupt sources. */

	s->private = subpriv;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
	if (has_int_sce) {
		s->n_chan = DIO200_MAX_ISNS;
		s->len_chanlist = DIO200_MAX_ISNS;
	} else {
		/* No interrupt source register.  Support single channel. */
		s->n_chan = 1;
		s->len_chanlist = 1;
	}
	s->range_table = &range_digital;
	s->maxdata = 1;
	s->insn_bits = dio200_subdev_intr_insn_bits;
	s->do_cmdtest = dio200_subdev_intr_cmdtest;
	s->do_cmd = dio200_subdev_intr_cmd;
	s->cancel = dio200_subdev_intr_cancel;

	return 0;
}

/*
 * This function cleans up an 'INTERRUPT' subdevice.
 */
static void
dio200_subdev_intr_cleanup(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;
	kfree(subpriv);
}

/*
 * Interrupt service routine.
 */
static irqreturn_t dio200_interrupt(int irq, void *d)
{
	struct comedi_device *dev = d;
	struct dio200_private *devpriv = dev->private;
	int handled;

	if (!dev->attached)
		return IRQ_NONE;

	if (devpriv->intr_sd >= 0) {
		handled = dio200_handle_read_intr(dev,
						  dev->subdevices +
						  devpriv->intr_sd);
	} else {
		handled = 0;
	}

	return IRQ_RETVAL(handled);
}

/*
 * Handle 'insn_read' for an '8254' counter subdevice.
 */
static int
dio200_subdev_8254_read(struct comedi_device *dev, struct comedi_subdevice *s,
			struct comedi_insn *insn, unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	data[0] = i8254_read(subpriv->iobase, 0, chan);
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 1;
}

/*
 * Handle 'insn_write' for an '8254' counter subdevice.
 */
static int
dio200_subdev_8254_write(struct comedi_device *dev, struct comedi_subdevice *s,
			 struct comedi_insn *insn, unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	i8254_write(subpriv->iobase, 0, chan, data[0]);
	spin_unlock_irqrestore(&subpriv->spinlock, flags);

	return 1;
}

/*
 * Set gate source for an '8254' counter subdevice channel.
 */
static int
dio200_set_gate_src(struct dio200_subdev_8254 *subpriv,
		    unsigned int counter_number, unsigned int gate_src)
{
	unsigned char byte;

	if (!subpriv->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;
	if (gate_src > 7)
		return -1;

	subpriv->gate_src[counter_number] = gate_src;
	byte = GAT_SCE(subpriv->which, counter_number, gate_src);
	outb(byte, subpriv->gat_sce_iobase);

	return 0;
}

/*
 * Get gate source for an '8254' counter subdevice channel.
 */
static int
dio200_get_gate_src(struct dio200_subdev_8254 *subpriv,
		    unsigned int counter_number)
{
	if (!subpriv->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;

	return subpriv->gate_src[counter_number];
}

/*
 * Set clock source for an '8254' counter subdevice channel.
 */
static int
dio200_set_clock_src(struct dio200_subdev_8254 *subpriv,
		     unsigned int counter_number, unsigned int clock_src)
{
	unsigned char byte;

	if (!subpriv->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;
	if (clock_src > 7)
		return -1;

	subpriv->clock_src[counter_number] = clock_src;
	byte = CLK_SCE(subpriv->which, counter_number, clock_src);
	outb(byte, subpriv->clk_sce_iobase);

	return 0;
}

/*
 * Get clock source for an '8254' counter subdevice channel.
 */
static int
dio200_get_clock_src(struct dio200_subdev_8254 *subpriv,
		     unsigned int counter_number, unsigned int *period_ns)
{
	unsigned clock_src;

	if (!subpriv->has_clk_gat_sce)
		return -1;
	if (counter_number > 2)
		return -1;

	clock_src = subpriv->clock_src[counter_number];
	*period_ns = clock_period[clock_src];
	return clock_src;
}

/*
 * Handle 'insn_config' for an '8254' counter subdevice.
 */
static int
dio200_subdev_8254_config(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	struct dio200_subdev_8254 *subpriv = s->private;
	int ret = 0;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long flags;

	spin_lock_irqsave(&subpriv->spinlock, flags);
	switch (data[0]) {
	case INSN_CONFIG_SET_COUNTER_MODE:
		ret = i8254_set_mode(subpriv->iobase, 0, chan, data[1]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_8254_READ_STATUS:
		data[1] = i8254_status(subpriv->iobase, 0, chan);
		break;
	case INSN_CONFIG_SET_GATE_SRC:
		ret = dio200_set_gate_src(subpriv, chan, data[2]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_GET_GATE_SRC:
		ret = dio200_get_gate_src(subpriv, chan);
		if (ret < 0) {
			ret = -EINVAL;
			break;
		}
		data[2] = ret;
		break;
	case INSN_CONFIG_SET_CLOCK_SRC:
		ret = dio200_set_clock_src(subpriv, chan, data[1]);
		if (ret < 0)
			ret = -EINVAL;
		break;
	case INSN_CONFIG_GET_CLOCK_SRC:
		ret = dio200_get_clock_src(subpriv, chan, &data[2]);
		if (ret < 0) {
			ret = -EINVAL;
			break;
		}
		data[1] = ret;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock_irqrestore(&subpriv->spinlock, flags);
	return ret < 0 ? ret : insn->n;
}

/*
 * This function initializes an '8254' counter subdevice.
 *
 * Note: iobase is the base address of the board, not the subdevice;
 * offset is the offset to the 8254 chip.
 */
static int
dio200_subdev_8254_init(struct comedi_device *dev, struct comedi_subdevice *s,
			unsigned long iobase, unsigned offset,
			int has_clk_gat_sce)
{
	struct dio200_subdev_8254 *subpriv;
	unsigned int chan;

	subpriv = kzalloc(sizeof(*subpriv), GFP_KERNEL);
	if (!subpriv) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return -ENOMEM;
	}

	s->private = subpriv;
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
	s->n_chan = 3;
	s->maxdata = 0xFFFF;
	s->insn_read = dio200_subdev_8254_read;
	s->insn_write = dio200_subdev_8254_write;
	s->insn_config = dio200_subdev_8254_config;

	spin_lock_init(&subpriv->spinlock);
	subpriv->iobase = offset + iobase;
	subpriv->has_clk_gat_sce = has_clk_gat_sce;
	if (has_clk_gat_sce) {
		/* Derive CLK_SCE and GAT_SCE register offsets from
		 * 8254 offset. */
		subpriv->clk_sce_iobase =
		    DIO200_XCLK_SCE + (offset >> 3) + iobase;
		subpriv->gat_sce_iobase =
		    DIO200_XGAT_SCE + (offset >> 3) + iobase;
		subpriv->which = (offset >> 2) & 1;
	}

	/* Initialize channels. */
	for (chan = 0; chan < 3; chan++) {
		i8254_set_mode(subpriv->iobase, 0, chan,
			       I8254_MODE0 | I8254_BINARY);
		if (subpriv->has_clk_gat_sce) {
			/* Gate source 0 is VCC (logic 1). */
			dio200_set_gate_src(subpriv, chan, 0);
			/* Clock source 0 is the dedicated clock input. */
			dio200_set_clock_src(subpriv, chan, 0);
		}
	}

	return 0;
}

/*
 * This function cleans up an '8254' counter subdevice.
 */
static void
dio200_subdev_8254_cleanup(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct dio200_subdev_intr *subpriv = s->private;
	kfree(subpriv);
}

static void dio200_report_attach(struct comedi_device *dev, unsigned int irq)
{
	const struct dio200_board *thisboard = comedi_board(dev);
	struct dio200_private *devpriv = dev->private;
	char tmpbuf[60];
	int tmplen;

	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA) &&
	    thisboard->bustype == isa_bustype)
		tmplen = scnprintf(tmpbuf, sizeof(tmpbuf),
				   "(base %#lx) ", dev->iobase);
	else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI) &&
		 thisboard->bustype == pci_bustype)
		tmplen = scnprintf(tmpbuf, sizeof(tmpbuf),
				   "(pci %s) ", pci_name(devpriv->pci_dev));
	else
		tmplen = 0;
	if (irq)
		tmplen += scnprintf(&tmpbuf[tmplen], sizeof(tmpbuf) - tmplen,
				    "(irq %u%s) ", irq,
				    (dev->irq ? "" : " UNAVAILABLE"));
	else
		tmplen += scnprintf(&tmpbuf[tmplen], sizeof(tmpbuf) - tmplen,
				    "(no irq) ");
	dev_info(dev->class_dev, "%s %sattached\n", dev->board_name, tmpbuf);
}

static int dio200_common_attach(struct comedi_device *dev, unsigned long iobase,
				unsigned int irq, unsigned long req_irq_flags)
{
	const struct dio200_board *thisboard = comedi_board(dev);
	struct dio200_private *devpriv = dev->private;
	const struct dio200_layout_struct *layout =
		&dio200_layouts[thisboard->layout];
	struct comedi_subdevice *s;
	int sdx;
	unsigned int n;
	int ret;

	devpriv->intr_sd = -1;
	dev->iobase = iobase;
	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, layout->n_subdevs);
	if (ret)
		return ret;

	for (n = 0; n < dev->n_subdevices; n++) {
		s = &dev->subdevices[n];
		switch (layout->sdtype[n]) {
		case sd_8254:
			/* counter subdevice (8254) */
			ret = dio200_subdev_8254_init(dev, s, iobase,
						      layout->sdinfo[n],
						      layout->has_clk_gat_sce);
			if (ret < 0)
				return ret;
			break;
		case sd_8255:
			/* digital i/o subdevice (8255) */
			ret = subdev_8255_init(dev, s, NULL,
					       iobase + layout->sdinfo[n]);
			if (ret < 0)
				return ret;
			break;
		case sd_intr:
			/* 'INTERRUPT' subdevice */
			if (irq) {
				ret = dio200_subdev_intr_init(dev, s,
							      iobase +
							      DIO200_INT_SCE,
							      layout->sdinfo[n],
							      layout->
							      has_int_sce);
				if (ret < 0)
					return ret;
				devpriv->intr_sd = n;
			} else {
				s->type = COMEDI_SUBD_UNUSED;
			}
			break;
		default:
			s->type = COMEDI_SUBD_UNUSED;
			break;
		}
	}
	sdx = devpriv->intr_sd;
	if (sdx >= 0 && sdx < dev->n_subdevices)
		dev->read_subdev = &dev->subdevices[sdx];
	if (irq) {
		if (request_irq(irq, dio200_interrupt, req_irq_flags,
				DIO200_DRIVER_NAME, dev) >= 0) {
			dev->irq = irq;
		} else {
			dev_warn(dev->class_dev,
				 "warning! irq %u unavailable!\n", irq);
		}
	}
	dio200_report_attach(dev, irq);
	return 1;
}

static int dio200_pci_common_attach(struct comedi_device *dev,
				    struct pci_dev *pci_dev)
{
	struct dio200_private *devpriv = dev->private;
	unsigned long iobase;
	int ret;

	devpriv->pci_dev = pci_dev;
	ret = comedi_pci_enable(pci_dev, DIO200_DRIVER_NAME);
	if (ret < 0) {
		dev_err(dev->class_dev,
			"error! cannot enable PCI device and request regions!\n");
		return ret;
	}
	iobase = pci_resource_start(pci_dev, 2);
	return dio200_common_attach(dev, iobase, pci_dev->irq, IRQF_SHARED);
}

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int dio200_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dio200_board *thisboard = comedi_board(dev);
	int ret;

	dev_info(dev->class_dev, DIO200_DRIVER_NAME ": attach\n");

	ret = alloc_private(dev, sizeof(struct dio200_private));
	if (ret < 0) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return ret;
	}

	/* Process options and reserve resources according to bus type. */
	if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA) &&
	    thisboard->bustype == isa_bustype) {
		unsigned long iobase;
		unsigned int irq;

		iobase = it->options[0];
		irq = it->options[1];
		ret = dio200_request_region(dev, iobase, DIO200_IO_SIZE);
		if (ret < 0)
			return ret;
		return dio200_common_attach(dev, iobase, irq, 0);
	} else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI) &&
		   thisboard->bustype == pci_bustype) {
		struct pci_dev *pci_dev;

		pci_dev = dio200_find_pci_dev(dev, it);
		if (!pci_dev)
			return -EIO;
		return dio200_pci_common_attach(dev, pci_dev);
	} else {
		dev_err(dev->class_dev, DIO200_DRIVER_NAME
			": BUG! cannot determine board type!\n");
		return -EINVAL;
	}
}

/*
 * The attach_pci hook (if non-NULL) is called at PCI probe time in preference
 * to the "manual" attach hook.  dev->board_ptr is NULL on entry.  There should
 * be a board entry matching the supplied PCI device.
 */
static int __devinit dio200_attach_pci(struct comedi_device *dev,
				       struct pci_dev *pci_dev)
{
	int ret;

	if (!IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI))
		return -EINVAL;

	dev_info(dev->class_dev, DIO200_DRIVER_NAME ": attach pci %s\n",
		 pci_name(pci_dev));
	ret = alloc_private(dev, sizeof(struct dio200_private));
	if (ret < 0) {
		dev_err(dev->class_dev, "error! out of memory!\n");
		return ret;
	}
	dev->board_ptr = dio200_find_pci_board(pci_dev);
	if (dev->board_ptr == NULL) {
		dev_err(dev->class_dev, "BUG! cannot determine board type!\n");
		return -EINVAL;
	}
	return dio200_pci_common_attach(dev, pci_dev);
}

static void dio200_detach(struct comedi_device *dev)
{
	const struct dio200_board *thisboard = comedi_board(dev);
	struct dio200_private *devpriv = dev->private;
	const struct dio200_layout_struct *layout;
	unsigned n;

	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->subdevices) {
		layout = &dio200_layouts[thisboard->layout];
		for (n = 0; n < dev->n_subdevices; n++) {
			struct comedi_subdevice *s = &dev->subdevices[n];
			switch (layout->sdtype[n]) {
			case sd_8254:
				dio200_subdev_8254_cleanup(dev, s);
				break;
			case sd_8255:
				subdev_8255_cleanup(dev, s);
				break;
			case sd_intr:
				dio200_subdev_intr_cleanup(dev, s);
				break;
			default:
				break;
			}
		}
	}
	if (devpriv) {
		if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI) &&
		    thisboard->bustype == pci_bustype) {
			if (devpriv->pci_dev) {
				if (dev->iobase)
					comedi_pci_disable(devpriv->pci_dev);
				pci_dev_put(devpriv->pci_dev);
			}
		} else if (IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_ISA) &&
			   thisboard->bustype == isa_bustype) {
			if (dev->iobase)
				release_region(dev->iobase, DIO200_IO_SIZE);
		}
	}
}

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static struct comedi_driver amplc_dio200_driver = {
	.driver_name = DIO200_DRIVER_NAME,
	.module = THIS_MODULE,
	.attach = dio200_attach,
	.attach_pci = dio200_attach_pci,
	.detach = dio200_detach,
	.board_name = &dio200_boards[0].name,
	.offset = sizeof(struct dio200_board),
	.num_names = ARRAY_SIZE(dio200_boards),
};

#if IS_ENABLED(CONFIG_COMEDI_AMPLC_DIO200_PCI)
static DEFINE_PCI_DEVICE_TABLE(dio200_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI215) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMPLICON, PCI_DEVICE_ID_AMPLICON_PCI272) },
	{0}
};

MODULE_DEVICE_TABLE(pci, dio200_pci_table);

static int __devinit amplc_dio200_pci_probe(struct pci_dev *dev,
						   const struct pci_device_id
						   *ent)
{
	return comedi_pci_auto_config(dev, &amplc_dio200_driver);
}

static void __devexit amplc_dio200_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static struct pci_driver amplc_dio200_pci_driver = {
	.name = DIO200_DRIVER_NAME,
	.id_table = dio200_pci_table,
	.probe = &amplc_dio200_pci_probe,
	.remove = __devexit_p(&amplc_dio200_pci_remove)
};
module_comedi_pci_driver(amplc_dio200_driver, amplc_dio200_pci_driver);
#else
module_comedi_driver(amplc_dio200_driver);
#endif

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
