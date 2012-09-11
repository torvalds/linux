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

Options:
 [0] - PCI bus number - if bus number and slot number are 0,
			then driver search for first unused card
 [1] - PCI slot number
*/

#include <linux/interrupt.h>
#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/pci.h>

#include "icp_multi.h"

#define PCI_DEVICE_ID_ICP_MULTI	0x8000

#define ICP_MULTI_EXTDEBUG

/*  Hardware types of the cards */
#define TYPE_ICP_MULTI	0

#define IORANGE_ICP_MULTI 	32

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
static unsigned short pci_list_builded;	/*>0 list of card is known */

struct boardtype {
	const char *name;	/*  driver name */
	int device_id;
	int iorange;		/*  I/O range len */
	char have_irq;		/*  1=card support IRQ */
	char cardtype;		/*  0=ICP Multi */
	int n_aichan;		/*  num of A/D chans */
	int n_aichand;		/*  num of A/D chans in diff mode */
	int n_aochan;		/*  num of D/A chans */
	int n_dichan;		/*  num of DI chans */
	int n_dochan;		/*  num of DO chans */
	int n_ctrs;		/*  num of counters */
	int ai_maxdata;		/*  resolution of A/D */
	int ao_maxdata;		/*  resolution of D/A */
	const struct comedi_lrange *rangelist_ai;	/*  rangelist for A/D */
	const char *rangecode;	/*  range codes for programming */
	const struct comedi_lrange *rangelist_ao;	/*  rangelist for D/A */
};

struct icp_multi_private {
	struct pcilst_struct *card;	/*  pointer to card */
	char valid;		/*  card is usable */
	void __iomem *io_addr;		/*  Pointer to mapped io address */
	resource_size_t phys_iobase;	/*  Physical io address */
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

#define devpriv ((struct icp_multi_private *)dev->private)
#define this_board ((const struct boardtype *)dev->board_ptr)

/*
==============================================================================

Name:	setup_channel_list

Description:
	This function sets the appropriate channel selection,
	differential input mode and range bits in the ADC Command/
	Status register.

Parameters:
	struct comedi_device *dev	Pointer to current service structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	unsigned int *chanlist	Pointer to packed channel list
	unsigned int n_chan	Number of channels to scan

Returns:Void

==============================================================================
*/
static void setup_channel_list(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       unsigned int *chanlist, unsigned int n_chan)
{
	unsigned int i, range, chanprog;
	unsigned int diff;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG:  setup_channel_list(...,%d)\n", n_chan);
#endif
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
		range = this_board->rangecode[CR_RANGE(chanlist[i])];
		/*  Set range. bits 4-5 */
		devpriv->AdcCmdStatus |= range;

		/* Output channel, range, mode to ICP Multi */
		writew(devpriv->AdcCmdStatus,
		       devpriv->io_addr + ICP_MULTI_ADC_CSR);

#ifdef ICP_MULTI_EXTDEBUG
		printk(KERN_DEBUG
		       "GS: %2d. [%4x]=%4x %4x\n", i, chanprog, range,
		       devpriv->act_chanlist[i]);
#endif
	}

}

/*
==============================================================================

Name:	icp_multi_insn_read_ai

Description:
	This function reads a single analogue input.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to analogue input data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_read_ai(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int n, timeout;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG "icp multi EDBG: BGN: icp_multi_insn_read_ai(...)\n");
#endif
	/*  Disable A/D conversion ready interrupt */
	devpriv->IntEnable &= ~ADC_READY;
	writew(devpriv->IntEnable, devpriv->io_addr + ICP_MULTI_INT_EN);

	/*  Clear interrupt status */
	devpriv->IntStatus |= ADC_READY;
	writew(devpriv->IntStatus, devpriv->io_addr + ICP_MULTI_INT_STAT);

	/*  Set up appropriate channel, mode and range data, for specified ch */
	setup_channel_list(dev, s, &insn->chanspec, 1);

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG "icp_multi A ST=%4x IO=%p\n",
	       readw(devpriv->io_addr + ICP_MULTI_ADC_CSR),
	       devpriv->io_addr + ICP_MULTI_ADC_CSR);
#endif

	for (n = 0; n < insn->n; n++) {
		/*  Set start ADC bit */
		devpriv->AdcCmdStatus |= ADC_ST;
		writew(devpriv->AdcCmdStatus,
		       devpriv->io_addr + ICP_MULTI_ADC_CSR);
		devpriv->AdcCmdStatus &= ~ADC_ST;

#ifdef ICP_MULTI_EXTDEBUG
		printk(KERN_DEBUG "icp multi B n=%d ST=%4x\n", n,
		       readw(devpriv->io_addr + ICP_MULTI_ADC_CSR));
#endif

		udelay(1);

#ifdef ICP_MULTI_EXTDEBUG
		printk(KERN_DEBUG "icp multi C n=%d ST=%4x\n", n,
		       readw(devpriv->io_addr + ICP_MULTI_ADC_CSR));
#endif

		/*  Wait for conversion to complete, or get fed up waiting */
		timeout = 100;
		while (timeout--) {
			if (!(readw(devpriv->io_addr +
				    ICP_MULTI_ADC_CSR) & ADC_BSY))
				goto conv_finish;

#ifdef ICP_MULTI_EXTDEBUG
			if (!(timeout % 10))
				printk(KERN_DEBUG
				       "icp multi D n=%d tm=%d ST=%4x\n", n,
				       timeout,
				       readw(devpriv->io_addr +
					     ICP_MULTI_ADC_CSR));
#endif

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

#ifdef ICP_MULTI_EXTDEBUG
		printk(KERN_DEBUG
		      "icp multi EDBG: END: icp_multi_insn_read_ai(...) n=%d\n",
		      n);
#endif
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

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: END: icp_multi_insn_read_ai(...) n=%d\n", n);
#endif
	return n;
}

/*
==============================================================================

Name:	icp_multi_insn_write_ao

Description:
	This function writes a single analogue output.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to analogue output data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_write_ao(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	int n, chan, range, timeout;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: BGN: icp_multi_insn_write_ao(...)\n");
#endif
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
	devpriv->DacCmdStatus |= this_board->rangecode[range];
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

#ifdef ICP_MULTI_EXTDEBUG
			if (!(timeout % 10))
				printk(KERN_DEBUG
				       "icp multi A n=%d tm=%d ST=%4x\n", n,
				       timeout,
				       readw(devpriv->io_addr +
					     ICP_MULTI_DAC_CSR));
#endif

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

#ifdef ICP_MULTI_EXTDEBUG
		printk(KERN_DEBUG
		     "icp multi EDBG: END: icp_multi_insn_write_ao(...) n=%d\n",
		     n);
#endif
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

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: END: icp_multi_insn_write_ao(...) n=%d\n", n);
#endif
	return n;
}

/*
==============================================================================

Name:	icp_multi_insn_read_ao

Description:
	This function reads a single analogue output.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to analogue output data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_read_ao(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	int n, chan;

	/*  Get channel number */
	chan = CR_CHAN(insn->chanspec);

	/*  Read analogue outputs */
	for (n = 0; n < insn->n; n++)
		data[n] = devpriv->ao_data[chan];

	return n;
}

/*
==============================================================================

Name:	icp_multi_insn_bits_di

Description:
	This function reads the digital inputs.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to analogue output data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_bits_di(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	data[1] = readw(devpriv->io_addr + ICP_MULTI_DI);

	return insn->n;
}

/*
==============================================================================

Name:	icp_multi_insn_bits_do

Description:
	This function writes the appropriate digital outputs.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to analogue output data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_bits_do(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG "icp multi EDBG: BGN: icp_multi_insn_bits_do(...)\n");
#endif

	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		printk(KERN_DEBUG "Digital outputs = %4x \n", s->state);

		writew(s->state, devpriv->io_addr + ICP_MULTI_DO);
	}

	data[1] = readw(devpriv->io_addr + ICP_MULTI_DI);

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG "icp multi EDBG: END: icp_multi_insn_bits_do(...)\n");
#endif
	return insn->n;
}

/*
==============================================================================

Name:	icp_multi_insn_read_ctr

Description:
	This function reads the specified counter.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to counter data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_read_ctr(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	return 0;
}

/*
==============================================================================

Name:	icp_multi_insn_write_ctr

Description:
	This function write to the specified counter.

Parameters:
	struct comedi_device *dev	Pointer to current device structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	struct comedi_insn *insn	Pointer to current comedi instruction
	unsigned int *data		Pointer to counter data

Returns:int			Nmuber of instructions executed

==============================================================================
*/
static int icp_multi_insn_write_ctr(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	return 0;
}

/*
==============================================================================

Name:	interrupt_service_icp_multi

Description:
	This function is the interrupt service routine for all
	interrupts generated by the icp multi board.

Parameters:
	int irq
	void *d			Pointer to current device

==============================================================================
*/
static irqreturn_t interrupt_service_icp_multi(int irq, void *d)
{
	struct comedi_device *dev = d;
	int int_no;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: BGN: interrupt_service_icp_multi(%d,...)\n",
	       irq);
#endif

	/*  Is this interrupt from our board? */
	int_no = readw(devpriv->io_addr + ICP_MULTI_INT_STAT) & Status_IRQ;
	if (!int_no)
		/*  No, exit */
		return IRQ_NONE;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: interrupt_service_icp_multi() ST: %4x\n",
	       readw(devpriv->io_addr + ICP_MULTI_INT_STAT));
#endif

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

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: END: interrupt_service_icp_multi(...)\n");
#endif
	return IRQ_HANDLED;
}

#if 0
/*
==============================================================================

Name:	check_channel_list

Description:
	This function checks if the channel list, provided by user
	is built correctly

Parameters:
	struct comedi_device *dev	Pointer to current service structure
	struct comedi_subdevice *s	Pointer to current subdevice structure
	unsigned int *chanlist	Pointer to packed channel list
	unsigned int n_chan	Number of channels to scan

Returns:int 0 = failure
	    1 = success

==============================================================================
*/
static int check_channel_list(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      unsigned int *chanlist, unsigned int n_chan)
{
	unsigned int i;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG:  check_channel_list(...,%d)\n", n_chan);
#endif
	/*  Check that we at least have one channel to check */
	if (n_chan < 1) {
		comedi_error(dev, "range/channel list is empty!");
		return 0;
	}
	/*  Check all channels */
	for (i = 0; i < n_chan; i++) {
		/*  Check that channel number is < maximum */
		if (CR_AREF(chanlist[i]) == AREF_DIFF) {
			if (CR_CHAN(chanlist[i]) > this_board->n_aichand) {
				comedi_error(dev,
					     "Incorrect differential ai ch-nr");
				return 0;
			}
		} else {
			if (CR_CHAN(chanlist[i]) > this_board->n_aichan) {
				comedi_error(dev,
					     "Incorrect ai channel number");
				return 0;
			}
		}
	}
	return 1;
}
#endif

/*
==============================================================================

Name:	icp_multi_reset

Description:
	This function resets the icp multi device to a 'safe' state

Parameters:
	struct comedi_device *dev	Pointer to current service structure

Returns:int	0 = success

==============================================================================
*/
static int icp_multi_reset(struct comedi_device *dev)
{
	unsigned int i;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp_multi EDBG: BGN: icp_multi_reset(...)\n");
#endif
	/*  Clear INT enables and requests */
	writew(0, devpriv->io_addr + ICP_MULTI_INT_EN);
	writew(0x00ff, devpriv->io_addr + ICP_MULTI_INT_STAT);

	if (this_board->n_aochan)
		/*  Set DACs to 0..5V range and 0V output */
		for (i = 0; i < this_board->n_aochan; i++) {
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
	/*  Digital outputs to 0 */
	writew(0, devpriv->io_addr + ICP_MULTI_DO);

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "icp multi EDBG: END: icp_multi_reset(...)\n");
#endif
	return 0;
}

static int icp_multi_attach(struct comedi_device *dev,
			    struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret, subdev, n_subdevices;
	unsigned int irq;
	struct pcilst_struct *card = NULL;
	resource_size_t io_addr[5], iobase;
	unsigned char pci_bus, pci_slot, pci_func;

	printk(KERN_WARNING
	       "icp_multi EDBG: BGN: icp_multi_attach(...)\n");

	/*  Allocate private data storage space */
	ret = alloc_private(dev, sizeof(struct icp_multi_private));
	if (ret < 0)
		return ret;

	/*  Initialise list of PCI cards in system, if not already done so */
	if (pci_list_builded++ == 0) {
		pci_card_list_init(PCI_VENDOR_ID_ICP,
#ifdef ICP_MULTI_EXTDEBUG
				   1
#else
				   0
#endif
		    );
	}

	printk(KERN_WARNING
	       "Anne's comedi%d: icp_multi: board=%s", dev->minor,
	       this_board->name);

	card = select_and_alloc_pci_card(PCI_VENDOR_ID_ICP,
					 this_board->device_id, it->options[0],
					 it->options[1]);

	if (card == NULL)
		return -EIO;

	devpriv->card = card;

	if ((pci_card_data(card, &pci_bus, &pci_slot, &pci_func, &io_addr[0],
			   &irq)) < 0) {
		printk(KERN_WARNING " - Can't get configuration data!\n");
		return -EIO;
	}

	iobase = io_addr[2];
	devpriv->phys_iobase = iobase;

	printk(KERN_WARNING
	       ", b:s:f=%d:%d:%d, io=0x%8llx \n", pci_bus, pci_slot, pci_func,
	       (unsigned long long)iobase);

	devpriv->io_addr = ioremap(iobase, ICP_MULTI_SIZE);

	if (devpriv->io_addr == NULL) {
		printk(KERN_WARNING "ioremap failed.\n");
		return -ENOMEM;
	}
#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG
	       "0x%08llx mapped to %p, ", (unsigned long long)iobase,
	       devpriv->io_addr);
#endif

	dev->board_name = this_board->name;

	n_subdevices = 0;
	if (this_board->n_aichan)
		n_subdevices++;
	if (this_board->n_aochan)
		n_subdevices++;
	if (this_board->n_dichan)
		n_subdevices++;
	if (this_board->n_dochan)
		n_subdevices++;
	if (this_board->n_ctrs)
		n_subdevices++;

	ret = comedi_alloc_subdevices(dev, n_subdevices);
	if (ret)
		return ret;

	icp_multi_reset(dev);

	if (this_board->have_irq) {
		if (irq) {
			if (request_irq(irq, interrupt_service_icp_multi,
					IRQF_SHARED, "Inova Icp Multi", dev)) {
				printk(KERN_WARNING
				    "unable to allocate IRQ %u, DISABLING IT",
				     irq);
				irq = 0;	/* Can't use IRQ */
			} else
				printk(KERN_WARNING ", irq=%u", irq);
		} else
			printk(KERN_WARNING ", IRQ disabled");
	} else
		irq = 0;

	dev->irq = irq;

	printk(KERN_WARNING ".\n");

	subdev = 0;

	if (this_board->n_aichan) {
		s = &dev->subdevices[subdev];
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_AI;
		s->subdev_flags = SDF_READABLE | SDF_COMMON | SDF_GROUND;
		if (this_board->n_aichand)
			s->subdev_flags |= SDF_DIFF;
		s->n_chan = this_board->n_aichan;
		s->maxdata = this_board->ai_maxdata;
		s->len_chanlist = this_board->n_aichan;
		s->range_table = this_board->rangelist_ai;
		s->insn_read = icp_multi_insn_read_ai;
		subdev++;
	}

	if (this_board->n_aochan) {
		s = &dev->subdevices[subdev];
		s->type = COMEDI_SUBD_AO;
		s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->n_aochan;
		s->maxdata = this_board->ao_maxdata;
		s->len_chanlist = this_board->n_aochan;
		s->range_table = this_board->rangelist_ao;
		s->insn_write = icp_multi_insn_write_ao;
		s->insn_read = icp_multi_insn_read_ao;
		subdev++;
	}

	if (this_board->n_dichan) {
		s = &dev->subdevices[subdev];
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE;
		s->n_chan = this_board->n_dichan;
		s->maxdata = 1;
		s->len_chanlist = this_board->n_dichan;
		s->range_table = &range_digital;
		s->io_bits = 0;
		s->insn_bits = icp_multi_insn_bits_di;
		subdev++;
	}

	if (this_board->n_dochan) {
		s = &dev->subdevices[subdev];
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE | SDF_READABLE;
		s->n_chan = this_board->n_dochan;
		s->maxdata = 1;
		s->len_chanlist = this_board->n_dochan;
		s->range_table = &range_digital;
		s->io_bits = (1 << this_board->n_dochan) - 1;
		s->state = 0;
		s->insn_bits = icp_multi_insn_bits_do;
		subdev++;
	}

	if (this_board->n_ctrs) {
		s = &dev->subdevices[subdev];
		s->type = COMEDI_SUBD_COUNTER;
		s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_COMMON;
		s->n_chan = this_board->n_ctrs;
		s->maxdata = 0xffff;
		s->len_chanlist = this_board->n_ctrs;
		s->state = 0;
		s->insn_read = icp_multi_insn_read_ctr;
		s->insn_write = icp_multi_insn_write_ctr;
		subdev++;
	}

	devpriv->valid = 1;

#ifdef ICP_MULTI_EXTDEBUG
	printk(KERN_DEBUG "icp multi EDBG: END: icp_multi_attach(...)\n");
#endif

	return 0;
}

static void icp_multi_detach(struct comedi_device *dev)
{
	if (dev->private)
		if (devpriv->valid)
			icp_multi_reset(dev);
	if (dev->irq)
		free_irq(dev->irq, dev);
	if (dev->private && devpriv->io_addr)
		iounmap(devpriv->io_addr);
	if (dev->private && devpriv->card)
		pci_card_free(devpriv->card);
	if (--pci_list_builded == 0)
		pci_card_list_cleanup(PCI_VENDOR_ID_ICP);
}

static const struct boardtype boardtypes[] = {
	{
		.name		= "icp_multi",
		.device_id	= PCI_DEVICE_ID_ICP_MULTI,
		.iorange	= IORANGE_ICP_MULTI,
		.have_irq	= 1,
		.cardtype	= TYPE_ICP_MULTI,
		.n_aichan	= 16,
		.n_aichand	= 8,
		.n_aochan	= 4,
		.n_dichan	= 16,
		.n_dochan	= 8,
		.n_ctrs		= 4,
		.ai_maxdata	= 0x0fff,
		.ao_maxdata	= 0x0fff,
		.rangelist_ai	= &range_analog,
		.rangecode	= range_codes_analog,
		.rangelist_ao	= &range_analog,
	},
};

static struct comedi_driver icp_multi_driver = {
	.driver_name	= "icp_multi",
	.module		= THIS_MODULE,
	.attach		= icp_multi_attach,
	.detach		= icp_multi_detach,
	.num_names	= ARRAY_SIZE(boardtypes),
	.board_name	= &boardtypes[0].name,
	.offset		= sizeof(struct boardtype),
};

static int __devinit icp_multi_pci_probe(struct pci_dev *dev,
					   const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &icp_multi_driver);
}

static void __devexit icp_multi_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
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
	.remove		= __devexit_p(icp_multi_pci_remove),
};
module_comedi_pci_driver(icp_multi_driver, icp_multi_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
