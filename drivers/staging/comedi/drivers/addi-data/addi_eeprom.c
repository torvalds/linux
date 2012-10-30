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

#define NVCMD_BEGIN_READ 	(0x7 << 5)	/*  nvRam begin read command */
#define NVCMD_LOAD_LOW   	(0x4 << 5)	/*  nvRam load low command */
#define NVCMD_LOAD_HIGH  	(0x5 << 5)	/*  nvRam load high command */

#define EE93C76_CLK_BIT		(1 << 0)
#define EE93C76_CS_BIT		(1 << 1)
#define EE93C76_DOUT_BIT	(1 << 2)
#define EE93C76_DIN_BIT		(1 << 3)
#define EE76_CMD_LEN    	13	/*  bits in instructions */
#define EE_READ         	0x0180	/*  01 1000 0000 read instruction */

#define EEPROM_DIGITALINPUT 			0
#define EEPROM_DIGITALOUTPUT			1
#define EEPROM_ANALOGINPUT				2
#define EEPROM_ANALOGOUTPUT				3
#define EEPROM_TIMER					4
#define EEPROM_WATCHDOG					5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10

struct str_Functionality {
	unsigned char b_Type;
	unsigned short w_Address;
};

struct str_MainHeader {
	unsigned short w_HeaderSize;
	unsigned char b_Nfunctions;
	struct str_Functionality s_Functions[7];
};

struct str_DigitalInputHeader {
	unsigned short w_Nchannel;
	unsigned char b_Interruptible;
	unsigned short w_NinterruptLogic;
};

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

static void v_EepromClock76(unsigned long iobase,
			    unsigned int dw_RegisterValue)
{
	outl(dw_RegisterValue & ~EE93C76_CLK_BIT, iobase);
	udelay(100);

	outl(dw_RegisterValue | EE93C76_CLK_BIT, iobase);
	udelay(100);
}

static void v_EepromSendCommand76(unsigned long iobase,
				  unsigned int dw_EepromCommand,
				  unsigned char b_DataLengthInBits)
{
	unsigned int dw_RegisterValue = EE93C76_CS_BIT;
	char c_BitPos = 0;

	/* Toggle EEPROM's Chip select to get it out of Shift Register Mode */
	outl(dw_RegisterValue, iobase);
	udelay(100);

	/* Send EEPROM command - one bit at a time */
	for (c_BitPos = (b_DataLengthInBits - 1); c_BitPos >= 0; c_BitPos--) {
		if (dw_EepromCommand & (1 << c_BitPos))
			dw_RegisterValue = dw_RegisterValue | EE93C76_DOUT_BIT;
		else
			dw_RegisterValue = dw_RegisterValue & ~EE93C76_DOUT_BIT;

		/* Write the command */
		outl(dw_RegisterValue, iobase);
		udelay(100);

		/* Trigger the EEPROM clock */
		v_EepromClock76(iobase, dw_RegisterValue);
	}
}

static void v_EepromCs76Read(unsigned long iobase,
			     unsigned short w_offset,
			     unsigned short *pw_Value)
{
        char c_BitPos = 0;
	unsigned int dw_RegisterValue = 0;
	unsigned int dw_RegisterValueRead = 0;

	/* Send EEPROM read command and offset to EEPROM */
	v_EepromSendCommand76(iobase, (EE_READ << 4) | (w_offset / 2),
		EE76_CMD_LEN);

	/* Get the last register value */
	dw_RegisterValue = (((w_offset / 2) & 0x1) << 2) | EE93C76_CS_BIT;

	/* Set the 16-bit value of 0 */
	*pw_Value = 0;

	/* Get the 16-bit value */
	for (c_BitPos = 0; c_BitPos < 16; c_BitPos++) {
		/* Trigger the EEPROM clock */
		v_EepromClock76(iobase, dw_RegisterValue);

		/* Get the result bit */
		dw_RegisterValueRead = inl(iobase);
		udelay(100);

		/* Get bit value and shift into result */
		if (dw_RegisterValueRead & EE93C76_DIN_BIT) {
			/* Read 1 */
			*pw_Value = (*pw_Value << 1) | 0x1;
		} else {
			/* Read 0 */
			*pw_Value = (*pw_Value << 1);
		}
	}

	/* Clear all EEPROM bits */
	dw_RegisterValue = 0x0;

	/* Toggle EEPROM's Chip select to get it out of Shift Register Mode */
	outl(dw_RegisterValue, iobase);
	udelay(100);
}

static void v_EepromWaitBusy(unsigned long iobase)
{
	unsigned char b_EepromBusy = 0;

	do {
		b_EepromBusy = inb(iobase + 0x3F);
		b_EepromBusy = b_EepromBusy & 0x80;
	} while (b_EepromBusy == 0x80);
}

static unsigned short w_EepromReadWord(unsigned long iobase,
				       char *type,
				       unsigned short w_EepromStartAddress)
{
	unsigned char b_Counter = 0;
	unsigned char b_ReadByte = 0;
	unsigned char b_ReadLowByte = 0;
	unsigned char b_ReadHighByte = 0;
	unsigned char b_SelectedAddressLow = 0;
	unsigned char b_SelectedAddressHigh = 0;
	unsigned short w_ReadWord = 0;

	/* Test the PCI chip type */
	if (!strcmp(type, "S5920") || !strcmp(type, "S5933")) {
		for (b_Counter = 0; b_Counter < 2; b_Counter++)
		{
			b_SelectedAddressLow = (w_EepromStartAddress + b_Counter) % 256;	/* Read the low 8 bit part */
			b_SelectedAddressHigh = (w_EepromStartAddress + b_Counter) / 256;	/* Read the high 8 bit part */

			/* Select the load low address mode */
			outb(NVCMD_LOAD_LOW, iobase + 0x3F);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Load the low address */
			outb(b_SelectedAddressLow, iobase + 0x3E);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Select the load high address mode */
			outb(NVCMD_LOAD_HIGH, iobase + 0x3F);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Load the high address */
			outb(b_SelectedAddressHigh, iobase + 0x3E);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Select the READ mode */
			outb(NVCMD_BEGIN_READ, iobase + 0x3F);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Read data into the EEPROM */
			b_ReadByte = inb(iobase + 0x3E);

			/* Wait on busy */
			v_EepromWaitBusy(iobase);

			/* Select the upper address part */
			if (b_Counter == 0)
			{
				b_ReadLowByte = b_ReadByte;
			}	/*  if(b_Counter==0) */
			else
			{
				b_ReadHighByte = b_ReadByte;
			}	/*  if(b_Counter==0) */
		}		/*  for (b_Counter=0; b_Counter<2; b_Counter++) */

		w_ReadWord = (b_ReadLowByte | (((unsigned short) b_ReadHighByte) * 256));
	}

	if (!strcmp(type, "93C76")) {
		/* Read 16 bit from the EEPROM 93C76 */
		v_EepromCs76Read(iobase, w_EepromStartAddress, &w_ReadWord);
	}

	return w_ReadWord;
}

static int i_EepromReadDigitalInputHeader(unsigned long iobase,
					  char *type,
					  unsigned short w_Address,
					  struct str_DigitalInputHeader *s_Header)
{
	unsigned short w_Temp;

	/*  read nbr of channels */
	s_Header->w_Nchannel = w_EepromReadWord(iobase, type,
						0x100 + w_Address + 6);

	/*  interruptible or not */
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 8);
	s_Header->b_Interruptible = (unsigned char) (w_Temp >> 7) & 0x01;

	/* How many interruptible logic */
	s_Header->w_NinterruptLogic = w_EepromReadWord(iobase, type,
						       0x100 + w_Address + 10);

	return 0;
}

static int i_EepromReadDigitalOutputHeader(unsigned long iobase,
					   char *type,
					   unsigned short w_Address,
					   struct str_DigitalOutputHeader *s_Header)
{
	/* Read Nbr channels */
	s_Header->w_Nchannel = w_EepromReadWord(iobase, type,
						0x100 + w_Address + 6);
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
	s_Header->w_Ntimer = w_EepromReadWord(iobase, type,
					      0x100 + w_Address + 6);
	/* Read header size */
	for (i = 0; i < s_Header->w_Ntimer; i++) {
		s_Header->s_TimerDetails[i].w_HeaderSize =
			w_EepromReadWord(iobase, type,
					 0x100 + w_Address + 8 + w_Size + 0);
		w_Temp = w_EepromReadWord(iobase, type,
					  0x100 + w_Address + 8 + w_Size + 2);

		/* Read Resolution */
		s_Header->s_TimerDetails[i].b_Resolution =
			(unsigned char) (w_Temp >> 10) & 0x3F;

		/* Read Mode */
		s_Header->s_TimerDetails[i].b_Mode =
			(unsigned char) (w_Temp >> 4) & 0x3F;

		w_Temp = w_EepromReadWord(iobase, type,
					  0x100 + w_Address + 8 + w_Size + 4);

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
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 10);
	s_Header->w_Nchannel = (w_Temp >> 4) & 0x03FF;
	/*  Resolution for 1st hard component */
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 16);
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
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 10);
	s_Header->w_Nchannel = (w_Temp >> 4) & 0x03FF;
	s_Header->w_MinConvertTiming = w_EepromReadWord(iobase, type,
							0x100 + w_Address + 16);
	s_Header->w_MinDelayTiming = w_EepromReadWord(iobase, type,
						      0x100 + w_Address + 30);
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 20);
	s_Header->b_HasDma = (w_Temp >> 13) & 0x01;	/*  whether dma present or not */

	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + 72);	/* reading Y */
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
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + w_Address + w_Offset);
	s_Header->b_Resolution = w_Temp & 0x001F;	/*  last 5 bits */

	return 0;
}

static int i_EepromReadMainHeader(unsigned long iobase,
				  char *type,
				  struct comedi_device *dev)
{
	const struct addi_board *this_board = comedi_board(dev);
	struct addi_private *devpriv = dev->private;
	unsigned short w_Temp, i, w_Count = 0;
	unsigned int ui_Temp;
	struct str_MainHeader s_MainHeader;
	struct str_DigitalInputHeader s_DigitalInputHeader;
	struct str_DigitalOutputHeader s_DigitalOutputHeader;
	/* struct str_TimerMainHeader     s_TimerMainHeader,s_WatchdogMainHeader; */
	struct str_AnalogOutputHeader s_AnalogOutputHeader;
	struct str_AnalogInputHeader s_AnalogInputHeader;

	/* Read size */
	s_MainHeader.w_HeaderSize = w_EepromReadWord(iobase, type,
						     0x100 + 8);

	/* Read nbr of functionality */
	w_Temp = w_EepromReadWord(iobase, type,
				  0x100 + 10);
	s_MainHeader.b_Nfunctions = (unsigned char) w_Temp & 0x00FF;

	/* Read functionality details */
	for (i = 0; i < s_MainHeader.b_Nfunctions; i++) {
		/* Read Type */
		w_Temp = w_EepromReadWord(iobase, type,
					  0x100 + 12 + w_Count);
		s_MainHeader.s_Functions[i].b_Type = (unsigned char) w_Temp & 0x3F;
		w_Count = w_Count + 2;
		/* Read Address */
		s_MainHeader.s_Functions[i].w_Address =
			w_EepromReadWord(iobase, type,
					 0x100 + 12 + w_Count);
		w_Count = w_Count + 2;
	}

	/* Display main header info */
	for (i = 0; i < s_MainHeader.b_Nfunctions; i++) {

		switch (s_MainHeader.s_Functions[i].b_Type) {
		case EEPROM_DIGITALINPUT:
			i_EepromReadDigitalInputHeader(iobase, type,
				s_MainHeader.s_Functions[i].w_Address,
				&s_DigitalInputHeader);
			devpriv->s_EeParameters.i_NbrDiChannel =
				s_DigitalInputHeader.w_Nchannel;
			break;

		case EEPROM_DIGITALOUTPUT:
			i_EepromReadDigitalOutputHeader(iobase, type,
				s_MainHeader.s_Functions[i].w_Address,
				&s_DigitalOutputHeader);
			devpriv->s_EeParameters.i_NbrDoChannel =
				s_DigitalOutputHeader.w_Nchannel;
			ui_Temp = 0xffffffff;
			devpriv->s_EeParameters.i_DoMaxdata =
				ui_Temp >> (32 -
					devpriv->s_EeParameters.i_NbrDoChannel);
			break;

		case EEPROM_ANALOGINPUT:
			i_EepromReadAnlogInputHeader(iobase, type,
				s_MainHeader.s_Functions[i].w_Address,
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
			i_EepromReadAnlogOutputHeader(iobase, type,
				s_MainHeader.s_Functions[i].w_Address,
				&s_AnalogOutputHeader);
			devpriv->s_EeParameters.i_NbrAoChannel =
				s_AnalogOutputHeader.w_Nchannel;
			ui_Temp = 0xffff;
			devpriv->s_EeParameters.i_AoMaxdata =
				ui_Temp >> (16 -
				s_AnalogOutputHeader.b_Resolution);
			break;

		case EEPROM_TIMER:
			/* Timer subdevice present */
			devpriv->s_EeParameters.i_Timer = 1;
			break;

		case EEPROM_WATCHDOG:
			/* Timer subdevice present */
			devpriv->s_EeParameters.i_Timer = 1;
			break;

		case EEPROM_TIMER_WATCHDOG_COUNTER:
			/* Timer subdevice present */
			devpriv->s_EeParameters.i_Timer = 1;
			break;
		}
	}

	return 0;
}
