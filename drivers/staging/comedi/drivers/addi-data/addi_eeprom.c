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
  | Project   : ADDI DATA         | Compiler : GCC 			              |
  | Modulname : addi_eeprom.c     | Version  : 2.96                       |
  +-------------------------------+---------------------------------------+
  | Project manager: Eric Stolz   | Date     :  02/12/2002                |
  +-----------------------------------------------------------------------+
  | Description : ADDI EEPROM  Module                                     |
  +-----------------------------------------------------------------------+
  |                             UPDATE'S                                  |
  +-----------------------------------------------------------------------+
  |   Date   |   Author  |          Description of updates                |
  +----------+-----------+------------------------------------------------+
  |          | 		 | 						  |
  |          |           | 						  |
  +----------+-----------+------------------------------------------------+
*/

#define NVRAM_USER_DATA_START	0x100

#define NVCMD_BEGIN_READ 	(0x7 << 5)	/*  nvRam begin read command */
#define NVCMD_LOAD_LOW   	(0x4 << 5)	/*  nvRam load low command */
#define NVCMD_LOAD_HIGH  	(0x5 << 5)	/*  nvRam load high command */

#define EE93C76_CLK_BIT		(1 << 0)
#define EE93C76_CS_BIT		(1 << 1)
#define EE93C76_DOUT_BIT	(1 << 2)
#define EE93C76_DIN_BIT		(1 << 3)
#define EE93C76_READ_CMD	(0x0180 << 4)
#define EE93C76_CMD_LEN		13

#define EEPROM_DIGITALINPUT 			0
#define EEPROM_DIGITALOUTPUT			1
#define EEPROM_ANALOGINPUT				2
#define EEPROM_ANALOGOUTPUT				3
#define EEPROM_TIMER					4
#define EEPROM_WATCHDOG					5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10

struct str_DigitalOutputHeader {
	unsigned short w_Nchannel;
};

/* used for timer as well as watchdog */
struct str_TimerDetails {
	unsigned short w_HeaderSize;
	unsigned char b_Resolution;
	unsigned char b_Mode;		/*  in case of Watchdog it is functionality */
	unsigned short w_MinTiming;
	unsigned char b_TimeBase;
};

struct str_TimerMainHeader {
	unsigned short w_Ntimer;
	struct str_TimerDetails s_TimerDetails[4];	/*   supports 4 timers */
};

struct str_AnalogOutputHeader {
	unsigned short w_Nchannel;
	unsigned char b_Resolution;
};

struct str_AnalogInputHeader {
	unsigned short w_Nchannel;
	unsigned short w_MinConvertTiming;
	unsigned short w_MinDelayTiming;
	unsigned char b_HasDma;
	unsigned char b_Resolution;
};

static void addi_eeprom_clk_93c76(unsigned long iobase, unsigned int val)
{
	outl(val & ~EE93C76_CLK_BIT, iobase);
	udelay(100);

	outl(val | EE93C76_CLK_BIT, iobase);
	udelay(100);
}

static unsigned int addi_eeprom_cmd_93c76(unsigned long iobase,
					  unsigned int cmd,
					  unsigned char len)
{
	unsigned int val = EE93C76_CS_BIT;
	int i;

	/* Toggle EEPROM's Chip select to get it out of Shift Register Mode */
	outl(val, iobase);
	udelay(100);

	/* Send EEPROM command - one bit at a time */
	for (i = (len - 1); i >= 0; i--) {
		if (cmd & (1 << i))
			val |= EE93C76_DOUT_BIT;
		else
			val &= ~EE93C76_DOUT_BIT;

		/* Write the command */
		outl(val, iobase);
		udelay(100);

		addi_eeprom_clk_93c76(iobase, val);
	}
	return val;
}

static unsigned short addi_eeprom_readw_93c76(unsigned long iobase,
					      unsigned short addr)
{
	unsigned short val = 0;
	unsigned int cmd;
	unsigned int tmp;
        int i;

	/* Send EEPROM read command and offset to EEPROM */
	cmd = EE93C76_READ_CMD | (addr / 2);
	cmd = addi_eeprom_cmd_93c76(iobase, cmd, EE93C76_CMD_LEN);

	/* Get the 16-bit value */
	for (i = 0; i < 16; i++) {
		addi_eeprom_clk_93c76(iobase, cmd);

		tmp = inl(iobase);
		udelay(100);

		val <<= 1;
		if (tmp & EE93C76_DIN_BIT)
			val |= 0x1;
	}

	/* Toggle EEPROM's Chip select to get it out of Shift Register Mode */
	outl(0, iobase);
	udelay(100);

	return val;
}

static void addi_eeprom_nvram_wait(unsigned long iobase)
{
	unsigned char val;

	do {
		val = inb(iobase + AMCC_OP_REG_MCSR_NVCMD);
	} while (val & 0x80);
}

static unsigned short addi_eeprom_readw_nvram(unsigned long iobase,
					      unsigned short addr)
{
	unsigned short val = 0;
	unsigned char tmp;
	unsigned char i;

	for (i = 0; i < 2; i++) {
		/* Load the low 8 bit address */
		outb(NVCMD_LOAD_LOW, iobase + AMCC_OP_REG_MCSR_NVCMD);
		addi_eeprom_nvram_wait(iobase);
		outb((addr + i) & 0xff, iobase + AMCC_OP_REG_MCSR_NVDATA);
		addi_eeprom_nvram_wait(iobase);

		/* Load the high 8 bit address */
		outb(NVCMD_LOAD_HIGH, iobase + AMCC_OP_REG_MCSR_NVCMD);
		addi_eeprom_nvram_wait(iobase);
		outb(((addr + i) >> 8) & 0xff, iobase + AMCC_OP_REG_MCSR_NVDATA);
		addi_eeprom_nvram_wait(iobase);

		/* Read the eeprom data byte */
		outb(NVCMD_BEGIN_READ, iobase + AMCC_OP_REG_MCSR_NVCMD);
		addi_eeprom_nvram_wait(iobase);
		tmp = inb(iobase + AMCC_OP_REG_MCSR_NVDATA);
		addi_eeprom_nvram_wait(iobase);

		if (i == 0)
			val |= tmp;
		else
			val |= (tmp << 8);
	}

	return val;
}

static unsigned short addi_eeprom_readw(unsigned long iobase,
					char *type,
					unsigned short addr)
{
	unsigned short val = 0;

	/* Add the offset to the start of the user data */
	addr += NVRAM_USER_DATA_START;

	if (!strcmp(type, "S5920") || !strcmp(type, "S5933"))
		val = addi_eeprom_readw_nvram(iobase, addr);

	if (!strcmp(type, "93C76"))
		val = addi_eeprom_readw_93c76(iobase, addr);

	return val;
}

static void addi_eeprom_read_di_info(struct comedi_device *dev,
				     unsigned long iobase,
				     char *type,
				     unsigned short addr)
{
	struct addi_private *devpriv = dev->private;
	unsigned short tmp;

	/* Number of channels */
	tmp = addi_eeprom_readw(iobase, type, addr + 6);
	devpriv->s_EeParameters.i_NbrDiChannel = tmp;

	/* Interruptible or not */
	tmp = addi_eeprom_readw(iobase, type, addr + 8);
	tmp = (tmp >> 7) & 0x01;

	/* How many interruptible logic */
	tmp = addi_eeprom_readw(iobase, type, addr + 10);
}

static int i_EepromReadDigitalOutputHeader(unsigned long iobase,
					   char *type,
					   unsigned short w_Address,
					   struct str_DigitalOutputHeader *s_Header)
{
	/* Read Nbr channels */
	s_Header->w_Nchannel = addi_eeprom_readw(iobase, type,
						 w_Address + 6);
	return 0;
}

#if 0
static int i_EepromReadTimerHeader(unsigned long iobase,
				   char *type,
				   unsigned short w_Address,
				   struct str_TimerMainHeader *s_Header)
{

	unsigned short i, w_Size = 0, w_Temp;

	/* Read No of Timer */
	s_Header->w_Ntimer = addi_eeprom_readw(iobase, type,
					       w_Address + 6);
	/* Read header size */
	for (i = 0; i < s_Header->w_Ntimer; i++) {
		s_Header->s_TimerDetails[i].w_HeaderSize =
			addi_eeprom_readw(iobase, type,
					  w_Address + 8 + w_Size + 0);
		w_Temp = addi_eeprom_readw(iobase, type,
					   w_Address + 8 + w_Size + 2);

		/* Read Resolution */
		s_Header->s_TimerDetails[i].b_Resolution =
			(unsigned char) (w_Temp >> 10) & 0x3F;

		/* Read Mode */
		s_Header->s_TimerDetails[i].b_Mode =
			(unsigned char) (w_Temp >> 4) & 0x3F;

		w_Temp = addi_eeprom_readw(iobase, type,
					   w_Address + 8 + w_Size + 4);

		/* Read MinTiming */
		s_Header->s_TimerDetails[i].w_MinTiming = (w_Temp >> 6) & 0x3FF;

		/* Read Timebase */
		s_Header->s_TimerDetails[i].b_TimeBase = (unsigned char) (w_Temp) & 0x3F;
		w_Size += s_Header->s_TimerDetails[i].w_HeaderSize;
	}

	return 0;
}
#endif

static int i_EepromReadAnlogOutputHeader(unsigned long iobase,
					 char *type,
					 unsigned short w_Address,
					 struct str_AnalogOutputHeader *s_Header)
{
	unsigned short w_Temp;

	/*  No of channels for 1st hard component */
	w_Temp = addi_eeprom_readw(iobase, type, w_Address + 10);
	s_Header->w_Nchannel = (w_Temp >> 4) & 0x03FF;
	/*  Resolution for 1st hard component */
	w_Temp = addi_eeprom_readw(iobase, type, w_Address + 16);
	s_Header->b_Resolution = (unsigned char) (w_Temp >> 8) & 0xFF;
	return 0;
}

/* Reads only for ONE  hardware component */
static int i_EepromReadAnlogInputHeader(unsigned long iobase,
					char *type,
					unsigned short w_Address,
					struct str_AnalogInputHeader *s_Header)
{
	unsigned short w_Temp, w_Offset;
	w_Temp = addi_eeprom_readw(iobase, type, w_Address + 10);
	s_Header->w_Nchannel = (w_Temp >> 4) & 0x03FF;
	s_Header->w_MinConvertTiming = addi_eeprom_readw(iobase, type,
							 w_Address + 16);
	s_Header->w_MinDelayTiming = addi_eeprom_readw(iobase, type,
						       w_Address + 30);
	w_Temp = addi_eeprom_readw(iobase, type,
				   w_Address + 20);
	s_Header->b_HasDma = (w_Temp >> 13) & 0x01;	/*  whether dma present or not */

	/* reading Y */
	w_Temp = addi_eeprom_readw(iobase, type, w_Address + 72);
	w_Temp = w_Temp & 0x00FF;
	if (w_Temp)		/* Y>0 */
	{
		w_Offset = 74 + (2 * w_Temp) + (10 * (1 + (w_Temp / 16)));	/*  offset of first analog input single header */
		w_Offset = w_Offset + 2;	/*  resolution */
	} else			/* Y=0 */
	{
		w_Offset = 74;
		w_Offset = w_Offset + 2;	/*  resolution */
	}

	/* read Resolution */
	w_Temp = addi_eeprom_readw(iobase, type, w_Address + w_Offset);
	s_Header->b_Resolution = w_Temp & 0x001F;	/*  last 5 bits */

	return 0;
}

static int i_EepromReadMainHeader(unsigned long iobase,
				  char *type,
				  struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;
	unsigned int ui_Temp;
	struct str_DigitalOutputHeader s_DigitalOutputHeader;
	/* struct str_TimerMainHeader     s_TimerMainHeader,s_WatchdogMainHeader; */
	struct str_AnalogOutputHeader s_AnalogOutputHeader;
	struct str_AnalogInputHeader s_AnalogInputHeader;
	unsigned short size;
	unsigned char nfuncs;
	int i;

	size = addi_eeprom_readw(iobase, type, 8);
	nfuncs = addi_eeprom_readw(iobase, type, 10) & 0xff;

	/* Read functionality details */
	for (i = 0; i < nfuncs; i++) {
		unsigned short offset = i * 4;
		unsigned short addr;
		unsigned char func;

		func = addi_eeprom_readw(iobase, type, 12 + offset) & 0x3f;
		addr = addi_eeprom_readw(iobase, type, 14 + offset);

		switch (func) {
		case EEPROM_DIGITALINPUT:
			addi_eeprom_read_di_info(dev, iobase, type, addr);
			break;

		case EEPROM_DIGITALOUTPUT:
			i_EepromReadDigitalOutputHeader(iobase, type, addr,
							&s_DigitalOutputHeader);

			devpriv->s_EeParameters.i_NbrDoChannel =
				s_DigitalOutputHeader.w_Nchannel;
			ui_Temp = 0xffffffff;
			devpriv->s_EeParameters.i_DoMaxdata =
				ui_Temp >> (32 -
					devpriv->s_EeParameters.i_NbrDoChannel);
			break;

		case EEPROM_ANALOGINPUT:
			i_EepromReadAnlogInputHeader(iobase, type, addr,
						     &s_AnalogInputHeader);

			if (!(strcmp(this_board->pc_DriverName, "apci3200")))
				devpriv->s_EeParameters.i_NbrAiChannel =
					s_AnalogInputHeader.w_Nchannel * 4;
			else
				devpriv->s_EeParameters.i_NbrAiChannel =
					s_AnalogInputHeader.w_Nchannel;
			devpriv->s_EeParameters.i_Dma =
				s_AnalogInputHeader.b_HasDma;
			devpriv->s_EeParameters.ui_MinAcquisitiontimeNs =
				(unsigned int) s_AnalogInputHeader.w_MinConvertTiming *
				1000;
			devpriv->s_EeParameters.ui_MinDelaytimeNs =
				(unsigned int) s_AnalogInputHeader.w_MinDelayTiming *
				1000;
			ui_Temp = 0xffff;
			devpriv->s_EeParameters.i_AiMaxdata =
				ui_Temp >> (16 -
				s_AnalogInputHeader.b_Resolution);
			break;

		case EEPROM_ANALOGOUTPUT:
			i_EepromReadAnlogOutputHeader(iobase, type, addr,
						      &s_AnalogOutputHeader);

			devpriv->s_EeParameters.i_NbrAoChannel =
				s_AnalogOutputHeader.w_Nchannel;
			ui_Temp = 0xffff;
			devpriv->s_EeParameters.i_AoMaxdata =
				ui_Temp >> (16 -
				s_AnalogOutputHeader.b_Resolution);
			break;

		case EEPROM_TIMER:
		case EEPROM_WATCHDOG:
		case EEPROM_TIMER_WATCHDOG_COUNTER:
			/* Timer subdevice present */
			devpriv->s_EeParameters.i_Timer = 1;
			break;
		}
	}

	return 0;
}
