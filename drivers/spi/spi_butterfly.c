/*
 * spi_butterfly.c - parport-to-butterfly adapter
 *
 * Copyright (C) 2005 David Brownell
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/parport.h>

#include <linux/sched.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/spi/flash.h>

#include <linux/mtd/partitions.h>


/*
 * This uses SPI to talk with an "AVR Butterfly", which is a $US20 card
 * with a battery powered AVR microcontroller and lots of goodies.  You
 * can use GCC to develop firmware for this.
 *
 * See Documentation/spi/butterfly for information about how to build
 * and use this custom parallel port cable.
 */


/* DATA output bits (pins 2..9 == D0..D7) */
#define	butterfly_nreset (1 << 1)		/* pin 3 */

#define	spi_sck_bit	(1 << 0)		/* pin 2 */
#define	spi_mosi_bit	(1 << 7)		/* pin 9 */

#define	vcc_bits	((1 << 6) | (1 << 5))	/* pins 7, 8 */

/* STATUS input bits */
#define	spi_miso_bit	PARPORT_STATUS_BUSY	/* pin 11 */

/* CONTROL output bits */
#define	spi_cs_bit	PARPORT_CONTROL_SELECT	/* pin 17 */



static inline struct butterfly *spidev_to_pp(struct spi_device *spi)
{
	return spi->controller_data;
}


struct butterfly {
	/* REVISIT ... for now, this must be first */
	struct spi_bitbang	bitbang;

	struct parport		*port;
	struct pardevice	*pd;

	u8			lastbyte;

	struct spi_device	*dataflash;
	struct spi_device	*butterfly;
	struct spi_board_info	info[2];

};

/*----------------------------------------------------------------------*/

static inline void
setsck(struct spi_device *spi, int is_on)
{
	struct butterfly	*pp = spidev_to_pp(spi);
	u8			bit, byte = pp->lastbyte;

	bit = spi_sck_bit;

	if (is_on)
		byte |= bit;
	else
		byte &= ~bit;
	parport_write_data(pp->port, byte);
	pp->lastbyte = byte;
}

static inline void
setmosi(struct spi_device *spi, int is_on)
{
	struct butterfly	*pp = spidev_to_pp(spi);
	u8			bit, byte = pp->lastbyte;

	bit = spi_mosi_bit;

	if (is_on)
		byte |= bit;
	else
		byte &= ~bit;
	parport_write_data(pp->port, byte);
	pp->lastbyte = byte;
}

static inline int getmiso(struct spi_device *spi)
{
	struct butterfly	*pp = spidev_to_pp(spi);
	int			value;
	u8			bit;

	bit = spi_miso_bit;

	/* only STATUS_BUSY is NOT negated */
	value = !(parport_read_status(pp->port) & bit);
	return (bit == PARPORT_STATUS_BUSY) ? value : !value;
}

static void butterfly_chipselect(struct spi_device *spi, int value)
{
	struct butterfly	*pp = spidev_to_pp(spi);

	/* set default clock polarity */
	if (value != BITBANG_CS_INACTIVE)
		setsck(spi, spi->mode & SPI_CPOL);

	/* here, value == "activate or not";
	 * most PARPORT_CONTROL_* bits are negated, so we must
	 * morph it to value == "bit value to write in control register"
	 */
	if (spi_cs_bit == PARPORT_CONTROL_INIT)
		value = !value;

	parport_frob_control(pp->port, spi_cs_bit, value ? spi_cs_bit : 0);
}


/* we only needed to implement one mode here, and choose SPI_MODE_0 */

#define	spidelay(X)	do{}while(0)
//#define	spidelay	ndelay

#define	EXPAND_BITBANG_TXRX
#include <linux/spi/spi_bitbang.h>

static u32
butterfly_txrx_word_mode0(struct spi_device *spi,
		unsigned nsecs,
		u32 word, u8 bits)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, word, bits);
}

/*----------------------------------------------------------------------*/

/* override default partitioning with cmdlinepart */
static struct mtd_partition partitions[] = { {
	/* JFFS2 wants partitions of 4*N blocks for this device,
	 * so sectors 0 and 1 can't be partitions by themselves.
	 */

	/* sector 0 = 8 pages * 264 bytes/page (1 block)
	 * sector 1 = 248 pages * 264 bytes/page
	 */
	.name		= "bookkeeping",	// 66 KB
	.offset		= 0,
	.size		= (8 + 248) * 264,
//	.mask_flags	= MTD_WRITEABLE,
}, {
	/* sector 2 = 256 pages * 264 bytes/page
	 * sectors 3-5 = 512 pages * 264 bytes/page
	 */
	.name		= "filesystem",		// 462 KB
	.offset		= MTDPART_OFS_APPEND,
	.size		= MTDPART_SIZ_FULL,
} };

static struct flash_platform_data flash = {
	.name		= "butterflash",
	.parts		= partitions,
	.nr_parts	= ARRAY_SIZE(partitions),
};


/* REVISIT remove this ugly global and its "only one" limitation */
static struct butterfly *butterfly;

static void butterfly_attach(struct parport *p)
{
	struct pardevice	*pd;
	int			status;
	struct butterfly	*pp;
	struct spi_master	*master;
	struct device		*dev = p->physport->dev;

	if (butterfly || !dev)
		return;

	/* REVISIT:  this just _assumes_ a butterfly is there ... no probe,
	 * and no way to be selective about what it binds to.
	 */

	master = spi_alloc_master(dev, sizeof *pp);
	if (!master) {
		status = -ENOMEM;
		goto done;
	}
	pp = spi_master_get_devdata(master);

	/*
	 * SPI and bitbang hookup
	 *
	 * use default setup(), cleanup(), and transfer() methods; and
	 * only bother implementing mode 0.  Start it later.
	 */
	master->bus_num = 42;
	master->num_chipselect = 2;

	pp->bitbang.master = spi_master_get(master);
	pp->bitbang.chipselect = butterfly_chipselect;
	pp->bitbang.txrx_word[SPI_MODE_0] = butterfly_txrx_word_mode0;

	/*
	 * parport hookup
	 */
	pp->port = p;
	pd = parport_register_device(p, "spi_butterfly",
			NULL, NULL, NULL,
			0 /* FLAGS */, pp);
	if (!pd) {
		status = -ENOMEM;
		goto clean0;
	}
	pp->pd = pd;

	status = parport_claim(pd);
	if (status < 0)
		goto clean1;

	/*
	 * Butterfly reset, powerup, run firmware
	 */
	pr_debug("%s: powerup/reset Butterfly\n", p->name);

	/* nCS for dataflash (this bit is inverted on output) */
	parport_frob_control(pp->port, spi_cs_bit, 0);

	/* stabilize power with chip in reset (nRESET), and
	 * spi_sck_bit clear (CPOL=0)
	 */
	pp->lastbyte |= vcc_bits;
	parport_write_data(pp->port, pp->lastbyte);
	msleep(5);

	/* take it out of reset; assume long reset delay */
	pp->lastbyte |= butterfly_nreset;
	parport_write_data(pp->port, pp->lastbyte);
	msleep(100);


	/*
	 * Start SPI ... for now, hide that we're two physical busses.
	 */
	status = spi_bitbang_start(&pp->bitbang);
	if (status < 0)
		goto clean2;

	/* Bus 1 lets us talk to at45db041b (firmware disables AVR SPI), AVR
	 * (firmware resets at45, acts as spi slave) or neither (we ignore
	 * both, AVR uses AT45).  Here we expect firmware for the first option.
	 */

	pp->info[0].max_speed_hz = 15 * 1000 * 1000;
	strcpy(pp->info[0].modalias, "mtd_dataflash");
	pp->info[0].platform_data = &flash;
	pp->info[0].chip_select = 1;
	pp->info[0].controller_data = pp;
	pp->dataflash = spi_new_device(pp->bitbang.master, &pp->info[0]);
	if (pp->dataflash)
		pr_debug("%s: dataflash at %s\n", p->name,
				dev_name(&pp->dataflash->dev));

	// dev_info(_what?_, ...)
	pr_info("%s: AVR Butterfly\n", p->name);
	butterfly = pp;
	return;

clean2:
	/* turn off VCC */
	parport_write_data(pp->port, 0);

	parport_release(pp->pd);
clean1:
	parport_unregister_device(pd);
clean0:
	(void) spi_master_put(pp->bitbang.master);
done:
	pr_debug("%s: butterfly probe, fail %d\n", p->name, status);
}

static void butterfly_detach(struct parport *p)
{
	struct butterfly	*pp;
	int			status;

	/* FIXME this global is ugly ... but, how to quickly get from
	 * the parport to the "struct butterfly" associated with it?
	 * "old school" driver-internal device lists?
	 */
	if (!butterfly || butterfly->port != p)
		return;
	pp = butterfly;
	butterfly = NULL;

	/* stop() unregisters child devices too */
	status = spi_bitbang_stop(&pp->bitbang);

	/* turn off VCC */
	parport_write_data(pp->port, 0);
	msleep(10);

	parport_release(pp->pd);
	parport_unregister_device(pp->pd);

	(void) spi_master_put(pp->bitbang.master);
}

static struct parport_driver butterfly_driver = {
	.name =		"spi_butterfly",
	.attach =	butterfly_attach,
	.detach =	butterfly_detach,
};


static int __init butterfly_init(void)
{
	return parport_register_driver(&butterfly_driver);
}
device_initcall(butterfly_init);

static void __exit butterfly_exit(void)
{
	parport_unregister_driver(&butterfly_driver);
}
module_exit(butterfly_exit);

MODULE_DESCRIPTION("Parport Adapter driver for AVR Butterfly");
MODULE_LICENSE("GPL");
