/* arch/arm/mach-lh7a40x/ssp-cpld.c
 *
 *  Copyright (C) 2004,2005 Marc Singer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 * SSP/SPI driver for the CardEngine CPLD.
 *
 */

/* NOTES
   -----

   o *** This driver is cribbed from the 7952x implementation.
	 Some comments may not apply.

   o This driver contains sufficient logic to control either the
     serial EEPROMs or the audio codec.  It is included in the kernel
     to support the codec.  The EEPROMs are really the responsibility
     of the boot loader and should probably be left alone.

   o The code must be augmented to cope with multiple, simultaneous
     clients.
     o The audio codec writes to the codec chip whenever playback
       starts.
     o The touchscreen driver writes to the ads chip every time it
       samples.
     o The audio codec must write 16 bits, but the touch chip writes
       are 8 bits long.
     o We need to be able to keep these configurations separate while
       simultaneously active.

 */

#include <linux/module.h>
#include <linux/kernel.h>
//#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
//#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <mach/hardware.h>

#include <mach/ssp.h>

//#define TALK

#if defined (TALK)
#define PRINTK(f...)		printk (f)
#else
#define PRINTK(f...)		do {} while (0)
#endif

#if defined (CONFIG_ARCH_LH7A400)
# define CPLD_SPID		__REGP16(CPLD06_VIRT) /* SPI data */
# define CPLD_SPIC		__REGP16(CPLD08_VIRT) /* SPI control */
# define CPLD_SPIC_CS_CODEC	(1<<0)
# define CPLD_SPIC_CS_TOUCH	(1<<1)
# define CPLD_SPIC_WRITE	(0<<2)
# define CPLD_SPIC_READ		(1<<2)
# define CPLD_SPIC_DONE		(1<<3) /* r/o */
# define CPLD_SPIC_LOAD		(1<<4)
# define CPLD_SPIC_START	(1<<4)
# define CPLD_SPIC_LOADED	(1<<5) /* r/o */
#endif

#define CPLD_SPI		__REGP16(CPLD0A_VIRT) /* SPI operation */
#define CPLD_SPI_CS_EEPROM	(1<<3)
#define CPLD_SPI_SCLK		(1<<2)
#define CPLD_SPI_TX_SHIFT	(1)
#define CPLD_SPI_TX		(1<<CPLD_SPI_TX_SHIFT)
#define CPLD_SPI_RX_SHIFT	(0)
#define CPLD_SPI_RX		(1<<CPLD_SPI_RX_SHIFT)

/* *** FIXME: these timing values are substantially larger than the
   *** chip requires. We may implement an nsleep () function. */
#define T_SKH	1		/* Clock time high (us) */
#define T_SKL	1		/* Clock time low (us) */
#define T_CS	1		/* Minimum chip select low time (us)  */
#define T_CSS	1		/* Minimum chip select setup time (us)  */
#define T_DIS	1		/* Data setup time (us) */

	 /* EEPROM SPI bits */
#define P_START		(1<<9)
#define P_WRITE		(1<<7)
#define P_READ		(2<<7)
#define P_ERASE		(3<<7)
#define P_EWDS		(0<<7)
#define P_WRAL		(0<<7)
#define P_ERAL		(0<<7)
#define P_EWEN		(0<<7)
#define P_A_EWDS	(0<<5)
#define P_A_WRAL	(1<<5)
#define P_A_ERAL	(2<<5)
#define P_A_EWEN	(3<<5)

struct ssp_configuration {
	int device;
	int mode;
	int speed;
	int frame_size_write;
	int frame_size_read;
};

static struct ssp_configuration ssp_configuration;
static spinlock_t ssp_lock;

static void enable_cs (void)
{
	switch (ssp_configuration.device) {
	case DEVICE_EEPROM:
		CPLD_SPI |= CPLD_SPI_CS_EEPROM;
		break;
	}
	udelay (T_CSS);
}

static void disable_cs (void)
{
	switch (ssp_configuration.device) {
	case DEVICE_EEPROM:
		CPLD_SPI &= ~CPLD_SPI_CS_EEPROM;
		break;
	}
	udelay (T_CS);
}

static void pulse_clock (void)
{
	CPLD_SPI |=  CPLD_SPI_SCLK;
	udelay (T_SKH);
	CPLD_SPI &= ~CPLD_SPI_SCLK;
	udelay (T_SKL);
}


/* execute_spi_command

   sends an spi command to a device.  It first sends cwrite bits from
   v.  If cread is greater than zero it will read cread bits
   (discarding the leading 0 bit) and return them.  If cread is less
   than zero it will check for completetion status and return 0 on
   success or -1 on timeout.  If cread is zero it does nothing other
   than sending the command.

   On the LPD7A400, we can only read or write multiples of 8 bits on
   the codec and the touch screen device.  Here, we round up.

*/

static int execute_spi_command (int v, int cwrite, int cread)
{
	unsigned long l = 0;

#if defined (CONFIG_MACH_LPD7A400)
	/* The codec and touch devices cannot be bit-banged.  Instead,
	 * the CPLD provides an eight-bit shift register and a crude
	 * interface.  */
	if (   ssp_configuration.device == DEVICE_CODEC
	    || ssp_configuration.device == DEVICE_TOUCH) {
		int select = 0;

		PRINTK ("spi(%d %d.%d) 0x%04x",
			ssp_configuration.device, cwrite, cread,
			v);
#if defined (TALK)
		if (ssp_configuration.device == DEVICE_CODEC)
			PRINTK (" 0x%03x -> %2d", v & 0x1ff, (v >> 9) & 0x7f);
#endif
		PRINTK ("\n");

		if (ssp_configuration.device == DEVICE_CODEC)
			select = CPLD_SPIC_CS_CODEC;
		if (ssp_configuration.device == DEVICE_TOUCH)
			select = CPLD_SPIC_CS_TOUCH;
		if (cwrite) {
			for (cwrite = (cwrite + 7)/8; cwrite-- > 0; ) {
				CPLD_SPID = (v >> (8*cwrite)) & 0xff;
				CPLD_SPIC = select | CPLD_SPIC_LOAD;
				while (!(CPLD_SPIC & CPLD_SPIC_LOADED))
					;
				CPLD_SPIC = select;
				while (!(CPLD_SPIC & CPLD_SPIC_DONE))
					;
			}
			v = 0;
		}
		if (cread) {
			mdelay (2);	/* *** FIXME: required by ads7843? */
			v = 0;
			for (cread = (cread + 7)/8; cread-- > 0;) {
				CPLD_SPID = 0;
				CPLD_SPIC = select | CPLD_SPIC_READ
					| CPLD_SPIC_START;
				while (!(CPLD_SPIC & CPLD_SPIC_LOADED))
					;
				CPLD_SPIC = select | CPLD_SPIC_READ;
				while (!(CPLD_SPIC & CPLD_SPIC_DONE))
					;
				v = (v << 8) | CPLD_SPID;
			}
		}
		return v;
	}
#endif

	PRINTK ("spi(%d) 0x%04x -> 0x%x\r\n", ssp_configuration.device,
		v & 0x1ff, (v >> 9) & 0x7f);

	enable_cs ();

	v <<= CPLD_SPI_TX_SHIFT; /* Correction for position of SPI_TX bit */
	while (cwrite--) {
		CPLD_SPI
			= (CPLD_SPI & ~CPLD_SPI_TX)
			| ((v >> cwrite) & CPLD_SPI_TX);
		udelay (T_DIS);
		pulse_clock ();
	}

	if (cread < 0) {
		int delay = 10;
		disable_cs ();
		udelay (1);
		enable_cs ();

		l = -1;
		do {
			if (CPLD_SPI & CPLD_SPI_RX) {
				l = 0;
				break;
			}
		} while (udelay (1), --delay);
	}
	else
	/* We pulse the clock before the data to skip the leading zero. */
		while (cread-- > 0) {
			pulse_clock ();
			l = (l<<1)
				| (((CPLD_SPI & CPLD_SPI_RX)
				    >> CPLD_SPI_RX_SHIFT) & 0x1);
		}

	disable_cs ();
	return l;
}

static int ssp_init (void)
{
	spin_lock_init (&ssp_lock);
	memset (&ssp_configuration, 0, sizeof (ssp_configuration));
	return 0;
}


/* ssp_chip_select

   drops the chip select line for the CPLD shift-register controlled
   devices.  It doesn't enable chip

*/

static void ssp_chip_select (int enable)
{
#if defined (CONFIG_MACH_LPD7A400)
	int select;

	if (ssp_configuration.device == DEVICE_CODEC)
		select = CPLD_SPIC_CS_CODEC;
	else if (ssp_configuration.device == DEVICE_TOUCH)
		select = CPLD_SPIC_CS_TOUCH;
	else
		return;

	if (enable)
		CPLD_SPIC = select;
	else
		CPLD_SPIC = 0;
#endif
}

static void ssp_acquire (void)
{
	spin_lock (&ssp_lock);
}

static void ssp_release (void)
{
	ssp_chip_select (0);	/* just in case */
	spin_unlock (&ssp_lock);
}

static int ssp_configure (int device, int mode, int speed,
			   int frame_size_write, int frame_size_read)
{
	ssp_configuration.device		= device;
	ssp_configuration.mode			= mode;
	ssp_configuration.speed			= speed;
	ssp_configuration.frame_size_write	= frame_size_write;
	ssp_configuration.frame_size_read	= frame_size_read;

	return 0;
}

static int ssp_read (void)
{
	return execute_spi_command (0, 0, ssp_configuration.frame_size_read);
}

static int ssp_write (u16 data)
{
	execute_spi_command (data, ssp_configuration.frame_size_write, 0);
	return 0;
}

static int ssp_write_read (u16 data)
{
	return execute_spi_command (data, ssp_configuration.frame_size_write,
				    ssp_configuration.frame_size_read);
}

struct ssp_driver lh7a40x_cpld_ssp_driver = {
	.init		= ssp_init,
	.acquire	= ssp_acquire,
	.release	= ssp_release,
	.configure	= ssp_configure,
	.chip_select	= ssp_chip_select,
	.read		= ssp_read,
	.write		= ssp_write,
	.write_read	= ssp_write_read,
};


MODULE_AUTHOR("Marc Singer");
MODULE_DESCRIPTION("LPD7A40X CPLD SPI driver");
MODULE_LICENSE("GPL");
