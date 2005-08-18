/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sym_glue.h"
#include "sym_nvram.h"

#ifdef	SYM_CONF_DEBUG_NVRAM
static u_char Tekram_boot_delay[7] = {3, 5, 10, 20, 30, 60, 120};
#endif

/*
 *  Get host setup from NVRAM.
 */
void sym_nvram_setup_host(struct Scsi_Host *shost, struct sym_hcb *np, struct sym_nvram *nvram)
{
	/*
	 *  Get parity checking, host ID, verbose mode 
	 *  and miscellaneous host flags from NVRAM.
	 */
	switch (nvram->type) {
	case SYM_SYMBIOS_NVRAM:
		if (!(nvram->data.Symbios.flags & SYMBIOS_PARITY_ENABLE))
			np->rv_scntl0  &= ~0x0a;
		np->myaddr = nvram->data.Symbios.host_id & 0x0f;
		if (nvram->data.Symbios.flags & SYMBIOS_VERBOSE_MSGS)
			np->verbose += 1;
		if (nvram->data.Symbios.flags1 & SYMBIOS_SCAN_HI_LO)
			shost->reverse_ordering = 1;
		if (nvram->data.Symbios.flags2 & SYMBIOS_AVOID_BUS_RESET)
			np->usrflags |= SYM_AVOID_BUS_RESET;
		break;
	case SYM_TEKRAM_NVRAM:
		np->myaddr = nvram->data.Tekram.host_id & 0x0f;
		break;
#ifdef CONFIG_PARISC
	case SYM_PARISC_PDC:
		if (nvram->data.parisc.host_id != -1)
			np->myaddr = nvram->data.parisc.host_id;
		if (nvram->data.parisc.factor != -1)
			np->minsync = nvram->data.parisc.factor;
		if (nvram->data.parisc.width != -1)
			np->maxwide = nvram->data.parisc.width;
		switch (nvram->data.parisc.mode) {
			case 0: np->scsi_mode = SMODE_SE; break;
			case 1: np->scsi_mode = SMODE_HVD; break;
			case 2: np->scsi_mode = SMODE_LVD; break;
			default: break;
		}
#endif
	default:
		break;
	}
}

/*
 *  Get target set-up from Symbios format NVRAM.
 */
static void
sym_Symbios_setup_target(struct sym_hcb *np, int target, Symbios_nvram *nvram)
{
	struct sym_tcb *tp = &np->target[target];
	Symbios_target *tn = &nvram->target[target];

	tp->usrtags =
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? SYM_SETUP_MAX_TAG : 0;

	if (!(tn->flags & SYMBIOS_DISCONNECT_ENABLE))
		tp->usrflags &= ~SYM_DISC_ENABLED;
	if (!(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME))
		tp->usrflags |= SYM_SCAN_BOOT_DISABLED;
	if (!(tn->flags & SYMBIOS_SCAN_LUNS))
		tp->usrflags |= SYM_SCAN_LUNS_DISABLED;
}

/*
 *  Get target set-up from Tekram format NVRAM.
 */
static void
sym_Tekram_setup_target(struct sym_hcb *np, int target, Tekram_nvram *nvram)
{
	struct sym_tcb *tp = &np->target[target];
	struct Tekram_target *tn = &nvram->target[target];

	if (tn->flags & TEKRAM_TAGGED_COMMANDS) {
		tp->usrtags = 2 << nvram->max_tags_index;
	}

	if (tn->flags & TEKRAM_DISCONNECT_ENABLE)
		tp->usrflags |= SYM_DISC_ENABLED;
 
	/* If any device does not support parity, we will not use this option */
	if (!(tn->flags & TEKRAM_PARITY_CHECK))
		np->rv_scntl0  &= ~0x0a; /* SCSI parity checking disabled */
}

/*
 *  Get target setup from NVRAM.
 */
void sym_nvram_setup_target(struct sym_hcb *np, int target, struct sym_nvram *nvp)
{
	switch (nvp->type) {
	case SYM_SYMBIOS_NVRAM:
		sym_Symbios_setup_target(np, target, &nvp->data.Symbios);
		break;
	case SYM_TEKRAM_NVRAM:
		sym_Tekram_setup_target(np, target, &nvp->data.Tekram);
		break;
	default:
		break;
	}
}

#ifdef	SYM_CONF_DEBUG_NVRAM
/*
 *  Dump Symbios format NVRAM for debugging purpose.
 */
static void sym_display_Symbios_nvram(struct sym_device *np, Symbios_nvram *nvram)
{
	int i;

	/* display Symbios nvram host data */
	printf("%s: HOST ID=%d%s%s%s%s%s%s\n",
		sym_name(np), nvram->host_id & 0x0f,
		(nvram->flags  & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags  & SYMBIOS_PARITY_ENABLE)	? " PARITY"	:"",
		(nvram->flags  & SYMBIOS_VERBOSE_MSGS)	? " VERBOSE"	:"", 
		(nvram->flags  & SYMBIOS_CHS_MAPPING)	? " CHS_ALT"	:"", 
		(nvram->flags2 & SYMBIOS_AVOID_BUS_RESET)?" NO_RESET"	:"",
		(nvram->flags1 & SYMBIOS_SCAN_HI_LO)	? " HI_LO"	:"");

	/* display Symbios nvram drive data */
	for (i = 0 ; i < 15 ; i++) {
		struct Symbios_target *tn = &nvram->target[i];
		printf("%s-%d:%s%s%s%s WIDTH=%d SYNC=%d TMO=%d\n",
		sym_name(np), i,
		(tn->flags & SYMBIOS_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & SYMBIOS_SCAN_AT_BOOT_TIME)	? " SCAN_BOOT"	: "",
		(tn->flags & SYMBIOS_SCAN_LUNS)		? " SCAN_LUNS"	: "",
		(tn->flags & SYMBIOS_QUEUE_TAGS_ENABLED)? " TCQ"	: "",
		tn->bus_width,
		tn->sync_period / 4,
		tn->timeout);
	}
}

/*
 *  Dump TEKRAM format NVRAM for debugging purpose.
 */
static void sym_display_Tekram_nvram(struct sym_device *np, Tekram_nvram *nvram)
{
	int i, tags, boot_delay;
	char *rem;

	/* display Tekram nvram host data */
	tags = 2 << nvram->max_tags_index;
	boot_delay = 0;
	if (nvram->boot_delay_index < 6)
		boot_delay = Tekram_boot_delay[nvram->boot_delay_index];
	switch ((nvram->flags & TEKRAM_REMOVABLE_FLAGS) >> 6) {
	default:
	case 0:	rem = "";			break;
	case 1: rem = " REMOVABLE=boot device";	break;
	case 2: rem = " REMOVABLE=all";		break;
	}

	printf("%s: HOST ID=%d%s%s%s%s%s%s%s%s%s BOOT DELAY=%d tags=%d\n",
		sym_name(np), nvram->host_id & 0x0f,
		(nvram->flags1 & SYMBIOS_SCAM_ENABLE)	? " SCAM"	:"",
		(nvram->flags & TEKRAM_MORE_THAN_2_DRIVES) ? " >2DRIVES":"",
		(nvram->flags & TEKRAM_DRIVES_SUP_1GB)	? " >1GB"	:"",
		(nvram->flags & TEKRAM_RESET_ON_POWER_ON) ? " RESET"	:"",
		(nvram->flags & TEKRAM_ACTIVE_NEGATION)	? " ACT_NEG"	:"",
		(nvram->flags & TEKRAM_IMMEDIATE_SEEK)	? " IMM_SEEK"	:"",
		(nvram->flags & TEKRAM_SCAN_LUNS)	? " SCAN_LUNS"	:"",
		(nvram->flags1 & TEKRAM_F2_F6_ENABLED)	? " F2_F6"	:"",
		rem, boot_delay, tags);

	/* display Tekram nvram drive data */
	for (i = 0; i <= 15; i++) {
		int sync, j;
		struct Tekram_target *tn = &nvram->target[i];
		j = tn->sync_index & 0xf;
		sync = Tekram_sync[j];
		printf("%s-%d:%s%s%s%s%s%s PERIOD=%d\n",
		sym_name(np), i,
		(tn->flags & TEKRAM_PARITY_CHECK)	? " PARITY"	: "",
		(tn->flags & TEKRAM_SYNC_NEGO)		? " SYNC"	: "",
		(tn->flags & TEKRAM_DISCONNECT_ENABLE)	? " DISC"	: "",
		(tn->flags & TEKRAM_START_CMD)		? " START"	: "",
		(tn->flags & TEKRAM_TAGGED_COMMANDS)	? " TCQ"	: "",
		(tn->flags & TEKRAM_WIDE_NEGO)		? " WIDE"	: "",
		sync);
	}
}
#else
static void sym_display_Symbios_nvram(struct sym_device *np, Symbios_nvram *nvram) { (void)np; (void)nvram; }
static void sym_display_Tekram_nvram(struct sym_device *np, Tekram_nvram *nvram) { (void)np; (void)nvram; }
#endif	/* SYM_CONF_DEBUG_NVRAM */


/*
 *  24C16 EEPROM reading.
 *
 *  GPOI0 - data in/data out
 *  GPIO1 - clock
 *  Symbios NVRAM wiring now also used by Tekram.
 */

#define SET_BIT 0
#define CLR_BIT 1
#define SET_CLK 2
#define CLR_CLK 3

/*
 *  Set/clear data/clock bit in GPIO0
 */
static void S24C16_set_bit(struct sym_device *np, u_char write_bit, u_char *gpreg, 
			  int bit_mode)
{
	udelay(5);
	switch (bit_mode) {
	case SET_BIT:
		*gpreg |= write_bit;
		break;
	case CLR_BIT:
		*gpreg &= 0xfe;
		break;
	case SET_CLK:
		*gpreg |= 0x02;
		break;
	case CLR_CLK:
		*gpreg &= 0xfd;
		break;

	}
	OUTB(np, nc_gpreg, *gpreg);
	INB(np, nc_mbox1);
	udelay(5);
}

/*
 *  Send START condition to NVRAM to wake it up.
 */
static void S24C16_start(struct sym_device *np, u_char *gpreg)
{
	S24C16_set_bit(np, 1, gpreg, SET_BIT);
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	S24C16_set_bit(np, 0, gpreg, CLR_BIT);
	S24C16_set_bit(np, 0, gpreg, CLR_CLK);
}

/*
 *  Send STOP condition to NVRAM - puts NVRAM to sleep... ZZzzzz!!
 */
static void S24C16_stop(struct sym_device *np, u_char *gpreg)
{
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	S24C16_set_bit(np, 1, gpreg, SET_BIT);
}

/*
 *  Read or write a bit to the NVRAM,
 *  read if GPIO0 input else write if GPIO0 output
 */
static void S24C16_do_bit(struct sym_device *np, u_char *read_bit, u_char write_bit, 
			 u_char *gpreg)
{
	S24C16_set_bit(np, write_bit, gpreg, SET_BIT);
	S24C16_set_bit(np, 0, gpreg, SET_CLK);
	if (read_bit)
		*read_bit = INB(np, nc_gpreg);
	S24C16_set_bit(np, 0, gpreg, CLR_CLK);
	S24C16_set_bit(np, 0, gpreg, CLR_BIT);
}

/*
 *  Output an ACK to the NVRAM after reading,
 *  change GPIO0 to output and when done back to an input
 */
static void S24C16_write_ack(struct sym_device *np, u_char write_bit, u_char *gpreg, 
			    u_char *gpcntl)
{
	OUTB(np, nc_gpcntl, *gpcntl & 0xfe);
	S24C16_do_bit(np, NULL, write_bit, gpreg);
	OUTB(np, nc_gpcntl, *gpcntl);
}

/*
 *  Input an ACK from NVRAM after writing,
 *  change GPIO0 to input and when done back to an output
 */
static void S24C16_read_ack(struct sym_device *np, u_char *read_bit, u_char *gpreg, 
			   u_char *gpcntl)
{
	OUTB(np, nc_gpcntl, *gpcntl | 0x01);
	S24C16_do_bit(np, read_bit, 1, gpreg);
	OUTB(np, nc_gpcntl, *gpcntl);
}

/*
 *  WRITE a byte to the NVRAM and then get an ACK to see it was accepted OK,
 *  GPIO0 must already be set as an output
 */
static void S24C16_write_byte(struct sym_device *np, u_char *ack_data, u_char write_data, 
			     u_char *gpreg, u_char *gpcntl)
{
	int x;
	
	for (x = 0; x < 8; x++)
		S24C16_do_bit(np, NULL, (write_data >> (7 - x)) & 0x01, gpreg);
		
	S24C16_read_ack(np, ack_data, gpreg, gpcntl);
}

/*
 *  READ a byte from the NVRAM and then send an ACK to say we have got it,
 *  GPIO0 must already be set as an input
 */
static void S24C16_read_byte(struct sym_device *np, u_char *read_data, u_char ack_data, 
			    u_char *gpreg, u_char *gpcntl)
{
	int x;
	u_char read_bit;

	*read_data = 0;
	for (x = 0; x < 8; x++) {
		S24C16_do_bit(np, &read_bit, 1, gpreg);
		*read_data |= ((read_bit & 0x01) << (7 - x));
	}

	S24C16_write_ack(np, ack_data, gpreg, gpcntl);
}

#ifdef SYM_CONF_NVRAM_WRITE_SUPPORT
/*
 *  Write 'len' bytes starting at 'offset'.
 */
static int sym_write_S24C16_nvram(struct sym_device *np, int offset,
		u_char *data, int len)
{
	u_char	gpcntl, gpreg;
	u_char	old_gpcntl, old_gpreg;
	u_char	ack_data;
	int	x;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB(np, nc_gpreg);
	old_gpcntl	= INB(np, nc_gpcntl);
	gpcntl		= old_gpcntl & 0x1c;

	/* set up GPREG & GPCNTL to set GPIO0 and GPIO1 in to known state */
	OUTB(np, nc_gpreg,  old_gpreg);
	OUTB(np, nc_gpcntl, gpcntl);

	/* this is to set NVRAM into a known state with GPIO0/1 both low */
	gpreg = old_gpreg;
	S24C16_set_bit(np, 0, &gpreg, CLR_CLK);
	S24C16_set_bit(np, 0, &gpreg, CLR_BIT);
		
	/* now set NVRAM inactive with GPIO0/1 both high */
	S24C16_stop(np, &gpreg);

	/* NVRAM has to be written in segments of 16 bytes */
	for (x = 0; x < len ; x += 16) {
		do {
			S24C16_start(np, &gpreg);
			S24C16_write_byte(np, &ack_data,
					  0xa0 | (((offset+x) >> 7) & 0x0e),
					  &gpreg, &gpcntl);
		} while (ack_data & 0x01);

		S24C16_write_byte(np, &ack_data, (offset+x) & 0xff, 
				  &gpreg, &gpcntl);

		for (y = 0; y < 16; y++)
			S24C16_write_byte(np, &ack_data, data[x+y], 
					  &gpreg, &gpcntl);
		S24C16_stop(np, &gpreg);
	}

	/* return GPIO0/1 to original states after having accessed NVRAM */
	OUTB(np, nc_gpcntl, old_gpcntl);
	OUTB(np, nc_gpreg,  old_gpreg);

	return 0;
}
#endif /* SYM_CONF_NVRAM_WRITE_SUPPORT */

/*
 *  Read 'len' bytes starting at 'offset'.
 */
static int sym_read_S24C16_nvram(struct sym_device *np, int offset, u_char *data, int len)
{
	u_char	gpcntl, gpreg;
	u_char	old_gpcntl, old_gpreg;
	u_char	ack_data;
	int	retv = 1;
	int	x;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB(np, nc_gpreg);
	old_gpcntl	= INB(np, nc_gpcntl);
	gpcntl		= old_gpcntl & 0x1c;

	/* set up GPREG & GPCNTL to set GPIO0 and GPIO1 in to known state */
	OUTB(np, nc_gpreg,  old_gpreg);
	OUTB(np, nc_gpcntl, gpcntl);

	/* this is to set NVRAM into a known state with GPIO0/1 both low */
	gpreg = old_gpreg;
	S24C16_set_bit(np, 0, &gpreg, CLR_CLK);
	S24C16_set_bit(np, 0, &gpreg, CLR_BIT);
		
	/* now set NVRAM inactive with GPIO0/1 both high */
	S24C16_stop(np, &gpreg);
	
	/* activate NVRAM */
	S24C16_start(np, &gpreg);

	/* write device code and random address MSB */
	S24C16_write_byte(np, &ack_data,
		0xa0 | ((offset >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* write random address LSB */
	S24C16_write_byte(np, &ack_data,
		offset & 0xff, &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* regenerate START state to set up for reading */
	S24C16_start(np, &gpreg);
	
	/* rewrite device code and address MSB with read bit set (lsb = 0x01) */
	S24C16_write_byte(np, &ack_data,
		0xa1 | ((offset >> 7) & 0x0e), &gpreg, &gpcntl);
	if (ack_data & 0x01)
		goto out;

	/* now set up GPIO0 for inputting data */
	gpcntl |= 0x01;
	OUTB(np, nc_gpcntl, gpcntl);
		
	/* input all requested data - only part of total NVRAM */
	for (x = 0; x < len; x++) 
		S24C16_read_byte(np, &data[x], (x == (len-1)), &gpreg, &gpcntl);

	/* finally put NVRAM back in inactive mode */
	gpcntl &= 0xfe;
	OUTB(np, nc_gpcntl, gpcntl);
	S24C16_stop(np, &gpreg);
	retv = 0;
out:
	/* return GPIO0/1 to original states after having accessed NVRAM */
	OUTB(np, nc_gpcntl, old_gpcntl);
	OUTB(np, nc_gpreg,  old_gpreg);

	return retv;
}

#undef SET_BIT
#undef CLR_BIT
#undef SET_CLK
#undef CLR_CLK

/*
 *  Try reading Symbios NVRAM.
 *  Return 0 if OK.
 */
static int sym_read_Symbios_nvram(struct sym_device *np, Symbios_nvram *nvram)
{
	static u_char Symbios_trailer[6] = {0xfe, 0xfe, 0, 0, 0, 0};
	u_char *data = (u_char *) nvram;
	int len  = sizeof(*nvram);
	u_short	csum;
	int x;

	/* probe the 24c16 and read the SYMBIOS 24c16 area */
	if (sym_read_S24C16_nvram (np, SYMBIOS_NVRAM_ADDRESS, data, len))
		return 1;

	/* check valid NVRAM signature, verify byte count and checksum */
	if (nvram->type != 0 ||
	    memcmp(nvram->trailer, Symbios_trailer, 6) ||
	    nvram->byte_count != len - 12)
		return 1;

	/* verify checksum */
	for (x = 6, csum = 0; x < len - 6; x++)
		csum += data[x];
	if (csum != nvram->checksum)
		return 1;

	return 0;
}

/*
 *  93C46 EEPROM reading.
 *
 *  GPOI0 - data in
 *  GPIO1 - data out
 *  GPIO2 - clock
 *  GPIO4 - chip select
 *
 *  Used by Tekram.
 */

/*
 *  Pulse clock bit in GPIO0
 */
static void T93C46_Clk(struct sym_device *np, u_char *gpreg)
{
	OUTB(np, nc_gpreg, *gpreg | 0x04);
	INB(np, nc_mbox1);
	udelay(2);
	OUTB(np, nc_gpreg, *gpreg);
}

/* 
 *  Read bit from NVRAM
 */
static void T93C46_Read_Bit(struct sym_device *np, u_char *read_bit, u_char *gpreg)
{
	udelay(2);
	T93C46_Clk(np, gpreg);
	*read_bit = INB(np, nc_gpreg);
}

/*
 *  Write bit to GPIO0
 */
static void T93C46_Write_Bit(struct sym_device *np, u_char write_bit, u_char *gpreg)
{
	if (write_bit & 0x01)
		*gpreg |= 0x02;
	else
		*gpreg &= 0xfd;
		
	*gpreg |= 0x10;
		
	OUTB(np, nc_gpreg, *gpreg);
	INB(np, nc_mbox1);
	udelay(2);

	T93C46_Clk(np, gpreg);
}

/*
 *  Send STOP condition to NVRAM - puts NVRAM to sleep... ZZZzzz!!
 */
static void T93C46_Stop(struct sym_device *np, u_char *gpreg)
{
	*gpreg &= 0xef;
	OUTB(np, nc_gpreg, *gpreg);
	INB(np, nc_mbox1);
	udelay(2);

	T93C46_Clk(np, gpreg);
}

/*
 *  Send read command and address to NVRAM
 */
static void T93C46_Send_Command(struct sym_device *np, u_short write_data, 
				u_char *read_bit, u_char *gpreg)
{
	int x;

	/* send 9 bits, start bit (1), command (2), address (6)  */
	for (x = 0; x < 9; x++)
		T93C46_Write_Bit(np, (u_char) (write_data >> (8 - x)), gpreg);

	*read_bit = INB(np, nc_gpreg);
}

/*
 *  READ 2 bytes from the NVRAM
 */
static void T93C46_Read_Word(struct sym_device *np,
		unsigned short *nvram_data, unsigned char *gpreg)
{
	int x;
	u_char read_bit;

	*nvram_data = 0;
	for (x = 0; x < 16; x++) {
		T93C46_Read_Bit(np, &read_bit, gpreg);

		if (read_bit & 0x01)
			*nvram_data |=  (0x01 << (15 - x));
		else
			*nvram_data &= ~(0x01 << (15 - x));
	}
}

/*
 *  Read Tekram NvRAM data.
 */
static int T93C46_Read_Data(struct sym_device *np, unsigned short *data,
		int len, unsigned char *gpreg)
{
	int x;

	for (x = 0; x < len; x++)  {
		unsigned char read_bit;
		/* output read command and address */
		T93C46_Send_Command(np, 0x180 | x, &read_bit, gpreg);
		if (read_bit & 0x01)
			return 1; /* Bad */
		T93C46_Read_Word(np, &data[x], gpreg);
		T93C46_Stop(np, gpreg);
	}

	return 0;
}

/*
 *  Try reading 93C46 Tekram NVRAM.
 */
static int sym_read_T93C46_nvram(struct sym_device *np, Tekram_nvram *nvram)
{
	u_char gpcntl, gpreg;
	u_char old_gpcntl, old_gpreg;
	int retv = 1;

	/* save current state of GPCNTL and GPREG */
	old_gpreg	= INB(np, nc_gpreg);
	old_gpcntl	= INB(np, nc_gpcntl);

	/* set up GPREG & GPCNTL to set GPIO0/1/2/4 in to known state, 0 in,
	   1/2/4 out */
	gpreg = old_gpreg & 0xe9;
	OUTB(np, nc_gpreg, gpreg);
	gpcntl = (old_gpcntl & 0xe9) | 0x09;
	OUTB(np, nc_gpcntl, gpcntl);

	/* input all of NVRAM, 64 words */
	retv = T93C46_Read_Data(np, (u_short *) nvram,
				sizeof(*nvram) / sizeof(short), &gpreg);
	
	/* return GPIO0/1/2/4 to original states after having accessed NVRAM */
	OUTB(np, nc_gpcntl, old_gpcntl);
	OUTB(np, nc_gpreg,  old_gpreg);

	return retv;
}

/*
 *  Try reading Tekram NVRAM.
 *  Return 0 if OK.
 */
static int sym_read_Tekram_nvram (struct sym_device *np, Tekram_nvram *nvram)
{
	u_char *data = (u_char *) nvram;
	int len = sizeof(*nvram);
	u_short	csum;
	int x;

	switch (np->device_id) {
	case PCI_DEVICE_ID_NCR_53C885:
	case PCI_DEVICE_ID_NCR_53C895:
	case PCI_DEVICE_ID_NCR_53C896:
		x = sym_read_S24C16_nvram(np, TEKRAM_24C16_NVRAM_ADDRESS,
					  data, len);
		break;
	case PCI_DEVICE_ID_NCR_53C875:
		x = sym_read_S24C16_nvram(np, TEKRAM_24C16_NVRAM_ADDRESS,
					  data, len);
		if (!x)
			break;
	default:
		x = sym_read_T93C46_nvram(np, nvram);
		break;
	}
	if (x)
		return 1;

	/* verify checksum */
	for (x = 0, csum = 0; x < len - 1; x += 2)
		csum += data[x] + (data[x+1] << 8);
	if (csum != 0x1234)
		return 1;

	return 0;
}

#ifdef CONFIG_PARISC
/*
 * Host firmware (PDC) keeps a table for altering SCSI capabilities.
 * Many newer machines export one channel of 53c896 chip as SE, 50-pin HD.
 * Also used for Multi-initiator SCSI clusters to set the SCSI Initiator ID.
 */
static int sym_read_parisc_pdc(struct sym_device *np, struct pdc_initiator *pdc)
{
	struct hardware_path hwpath;
	get_pci_node_path(np->pdev, &hwpath);
	if (!pdc_get_initiator(&hwpath, pdc))
		return 0;

	return SYM_PARISC_PDC;
}
#else
static inline int sym_read_parisc_pdc(struct sym_device *np,
					struct pdc_initiator *x)
{
	return 0;
}
#endif

/*
 *  Try reading Symbios or Tekram NVRAM
 */
int sym_read_nvram(struct sym_device *np, struct sym_nvram *nvp)
{
	if (!sym_read_Symbios_nvram(np, &nvp->data.Symbios)) {
		nvp->type = SYM_SYMBIOS_NVRAM;
		sym_display_Symbios_nvram(np, &nvp->data.Symbios);
	} else if (!sym_read_Tekram_nvram(np, &nvp->data.Tekram)) {
		nvp->type = SYM_TEKRAM_NVRAM;
		sym_display_Tekram_nvram(np, &nvp->data.Tekram);
	} else {
		nvp->type = sym_read_parisc_pdc(np, &nvp->data.parisc);
	}
	return nvp->type;
}

char *sym_nvram_type(struct sym_nvram *nvp)
{
	switch (nvp->type) {
	case SYM_SYMBIOS_NVRAM:
		return "Symbios NVRAM";
	case SYM_TEKRAM_NVRAM:
		return "Tekram NVRAM";
	case SYM_PARISC_PDC:
		return "PA-RISC Firmware";
	default:
		return "No NVRAM";
	}
}
