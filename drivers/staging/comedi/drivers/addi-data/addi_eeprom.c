/*
 * addi_eeprom.c - ADDI EEPROM Module
 * Copyright (C) 2004,2005  ADDI-DATA GmbH for the source code of this module.
 * Project manager: Eric Stolz
 *
 *	ADDI-DATA GmbH
 *	Dieselstrasse 3
 *	D-77833 Ottersweier
 *	Tel: +19(0)7223/9493-0
 *	Fax: +49(0)7223/9493-92
 *	http://www.addi-data.com
 *	info@addi-data.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include <linux/delay.h>

#define NVRAM_USER_DATA_START	0x100

#define NVCMD_BEGIN_READ	(0x7 << 5)	/* nvRam begin read command */
#define NVCMD_LOAD_LOW		(0x4 << 5)	/* nvRam load low command */
#define NVCMD_LOAD_HIGH		(0x5 << 5)	/* nvRam load high command */

#define EE93C76_CLK_BIT		(1 << 0)
#define EE93C76_CS_BIT		(1 << 1)
#define EE93C76_DOUT_BIT	(1 << 2)
#define EE93C76_DIN_BIT		(1 << 3)
#define EE93C76_READ_CMD	(0x0180 << 4)
#define EE93C76_CMD_LEN		13

#define EEPROM_DIGITALINPUT		0
#define EEPROM_DIGITALOUTPUT		1
#define EEPROM_ANALOGINPUT		2
#define EEPROM_ANALOGOUTPUT		3
#define EEPROM_TIMER			4
#define EEPROM_WATCHDOG			5
#define EEPROM_TIMER_WATCHDOG_COUNTER	10

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
		outb(((addr + i) >> 8) & 0xff,
			iobase + AMCC_OP_REG_MCSR_NVDATA);
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
				     unsigned short addr)
{
	const struct addi_board *this_board = dev->board_ptr;
	struct addi_private *devpriv = dev->private;
	char *type = this_board->pc_EepromChip;
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

static void addi_eeprom_read_do_info(struct comedi_device *dev,
				     unsigned long iobase,
				     unsigned short addr)
{
	const struct addi_board *this_board = dev->board_ptr;
	struct addi_private *devpriv = dev->private;
	char *type = this_board->pc_EepromChip;
	unsigned short tmp;

	/* Number of channels */
	tmp = addi_eeprom_readw(iobase, type, addr + 6);
	devpriv->s_EeParameters.i_NbrDoChannel = tmp;

	devpriv->s_EeParameters.i_DoMaxdata = 0xffffffff >> (32 - tmp);
}

static void addi_eeprom_read_timer_info(struct comedi_device *dev,
					unsigned long iobase,
					unsigned short addr)
{
	struct addi_private *devpriv = dev->private;
#if 0
	const struct addi_board *this_board = dev->board_ptr;
	char *type = this_board->pc_EepromChip;
	unsigned short offset = 0;
	unsigned short ntimers;
	unsigned short tmp;
	int i;

	/* Number of Timers */
	ntimers = addi_eeprom_readw(iobase, type, addr + 6);

	/* Read header size */
	for (i = 0; i < ntimers; i++) {
		unsigned short size;
		unsigned short res;
		unsigned short mode;
		unsigned short min_timing;
		unsigned short timebase;

		size = addi_eeprom_readw(iobase, type, addr + 8 + offset + 0);

		/* Resolution / Mode */
		tmp = addi_eeprom_readw(iobase, type, addr + 8 + offset + 2);
		res = (tmp >> 10) & 0x3f;
		mode = (tmp >> 4) & 0x3f;

		/* MinTiming / Timebase */
		tmp = addi_eeprom_readw(iobase, type, addr + 8 + offset + 4);
		min_timing = (tmp  >> 6) & 0x3ff;
		Timebase = tmp & 0x3f;

		offset += size;
	}
#endif
	/* Timer subdevice present */
	devpriv->s_EeParameters.i_Timer = 1;
}

static void addi_eeprom_read_ao_info(struct comedi_device *dev,
				     unsigned long iobase,
				     unsigned short addr)
{
	const struct addi_board *this_board = dev->board_ptr;
	struct addi_private *devpriv = dev->private;
	char *type = this_board->pc_EepromChip;
	unsigned short tmp;

	/* No of channels for 1st hard component */
	tmp = addi_eeprom_readw(iobase, type, addr + 10);
	devpriv->s_EeParameters.i_NbrAoChannel = (tmp >> 4) & 0x3ff;

	/* Resolution for 1st hard component */
	tmp = addi_eeprom_readw(iobase, type, addr + 16);
	tmp = (tmp >> 8) & 0xff;
	devpriv->s_EeParameters.i_AoMaxdata = 0xfff >> (16 - tmp);
}

static void addi_eeprom_read_ai_info(struct comedi_device *dev,
				     unsigned long iobase,
				     unsigned short addr)
{
	const struct addi_board *this_board = dev->board_ptr;
	struct addi_private *devpriv = dev->private;
	char *type = this_board->pc_EepromChip;
	unsigned short offset;
	unsigned short tmp;

	/* No of channels for 1st hard component */
	tmp = addi_eeprom_readw(iobase, type, addr + 10);
	devpriv->s_EeParameters.i_NbrAiChannel = (tmp >> 4) & 0x3ff;
	if (!strcmp(this_board->pc_DriverName, "apci3200"))
		devpriv->s_EeParameters.i_NbrAiChannel *= 4;

	tmp = addi_eeprom_readw(iobase, type, addr + 16);
	devpriv->s_EeParameters.ui_MinAcquisitiontimeNs = tmp * 1000;

	tmp = addi_eeprom_readw(iobase, type, addr + 30);
	devpriv->s_EeParameters.ui_MinDelaytimeNs = tmp * 1000;

	tmp = addi_eeprom_readw(iobase, type, addr + 20);
	/* dma = (tmp >> 13) & 0x01; */

	tmp = addi_eeprom_readw(iobase, type, addr + 72) & 0xff;
	if (tmp) {		/* > 0 */
		/* offset of first analog input single header */
		offset = 74 + (2 * tmp) + (10 * (1 + (tmp / 16)));
	} else {		/* = 0 */
		offset = 74;
	}

	/* Resolution */
	tmp = addi_eeprom_readw(iobase, type, addr + offset + 2) & 0x1f;
	devpriv->s_EeParameters.i_AiMaxdata = 0xffff >> (16 - tmp);
}

static void addi_eeprom_read_info(struct comedi_device *dev,
				  unsigned long iobase)
{
	const struct addi_board *this_board = dev->board_ptr;
	char *type = this_board->pc_EepromChip;
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
			addi_eeprom_read_di_info(dev, iobase, addr);
			break;

		case EEPROM_DIGITALOUTPUT:
			addi_eeprom_read_do_info(dev, iobase, addr);
			break;

		case EEPROM_ANALOGINPUT:
			addi_eeprom_read_ai_info(dev, iobase, addr);
			break;

		case EEPROM_ANALOGOUTPUT:
			addi_eeprom_read_ao_info(dev, iobase, addr);
			break;

		case EEPROM_TIMER:
		case EEPROM_WATCHDOG:
		case EEPROM_TIMER_WATCHDOG_COUNTER:
			addi_eeprom_read_timer_info(dev, iobase, addr);
			break;
		}
	}
}
