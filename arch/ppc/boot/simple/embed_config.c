/* Board specific functions for those embedded 8xx boards that do
 * not have boot monitor support for board information.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/types.h>
#include <linux/config.h>
#include <linux/string.h>
#include <asm/reg.h>
#ifdef CONFIG_8xx
#include <asm/mpc8xx.h>
#endif
#ifdef CONFIG_8260
#include <asm/mpc8260.h>
#include <asm/immap_cpm2.h>
#endif
#ifdef CONFIG_40x
#include <asm/io.h>
#endif
extern unsigned long timebase_period_ns;

/* For those boards that don't provide one.
*/
#if !defined(CONFIG_MBX)
static	bd_t	bdinfo;
#endif

/* IIC functions.
 * These are just the basic master read/write operations so we can
 * examine serial EEPROM.
 */
extern void	iic_read(uint devaddr, u_char *buf, uint offset, uint count);

/* Supply a default Ethernet address for those eval boards that don't
 * ship with one.  This is an address from the MBX board I have, so
 * it is unlikely you will find it on your network.
 */
static	ushort	def_enet_addr[] = { 0x0800, 0x3e26, 0x1559 };

#if defined(CONFIG_MBX)

/* The MBX hands us a pretty much ready to go board descriptor.  This
 * is where the idea started in the first place.
 */
void
embed_config(bd_t **bdp)
{
	u_char	*mp;
	u_char	eebuf[128];
	int i = 8;
	bd_t    *bd;

	bd = *bdp;

	/* Read the first 128 bytes of the EEPROM.  There is more,
	 * but this is all we need.
	 */
	iic_read(0xa4, eebuf, 0, 128);

	/* All we are looking for is the Ethernet MAC address.  The
	 * first 8 bytes are 'MOTOROLA', so check for part of that.
	 * Next, the VPD describes a MAC 'packet' as being of type 08
	 * and size 06.  So we look for that and the MAC must follow.
	 * If there are more than one, we still only care about the first.
	 * If it's there, assume we have a valid MAC address.  If not,
	 * grab our default one.
	 */
	if ((*(uint *)eebuf) == 0x4d4f544f) {
		while (i < 127 && !(eebuf[i] == 0x08 && eebuf[i + 1] == 0x06))
			 i += eebuf[i + 1] + 2;  /* skip this packet */

		if (i == 127)	/* Couldn't find. */
			mp = (u_char *)def_enet_addr;
		else
			mp = &eebuf[i + 2];
	}
	else
		mp = (u_char *)def_enet_addr;

	for (i=0; i<6; i++)
		bd->bi_enetaddr[i] = *mp++;

	/* The boot rom passes these to us in MHz.  Linux now expects
	 * them to be in Hz.
	 */
	bd->bi_intfreq *= 1000000;
	bd->bi_busfreq *= 1000000;

	/* Stuff a baud rate here as well.
	*/
	bd->bi_baudrate = 9600;
}
#endif /* CONFIG_MBX */

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC) || \
	defined(CONFIG_RPX8260) || defined(CONFIG_EP405)
/* Helper functions for Embedded Planet boards.
*/
/* Because I didn't find anything that would do this.......
*/
u_char
aschex_to_byte(u_char *cp)
{
	u_char	byte, c;

	c = *cp++;

	if ((c >= 'A') && (c <= 'F')) {
		c -= 'A';
		c += 10;
	} else if ((c >= 'a') && (c <= 'f')) {
		c -= 'a';
		c += 10;
	} else
		c -= '0';

	byte = c * 16;

	c = *cp;

	if ((c >= 'A') && (c <= 'F')) {
		c -= 'A';
		c += 10;
	} else if ((c >= 'a') && (c <= 'f')) {
		c -= 'a';
		c += 10;
	} else
		c -= '0';

	byte += c;

	return(byte);
}

static void
rpx_eth(bd_t *bd, u_char *cp)
{
	int	i;

	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = aschex_to_byte(cp);
		cp += 2;
	}
}

#ifdef CONFIG_RPX8260
static uint
rpx_baseten(u_char *cp)
{
	uint	retval;

	retval = 0;

	while (*cp != '\n') {
		retval *= 10;
		retval += (*cp) - '0';
		cp++;
	}
	return(retval);
}
#endif

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)
static void
rpx_brate(bd_t *bd, u_char *cp)
{
	uint	rate;

	rate = 0;

	while (*cp != '\n') {
		rate *= 10;
		rate += (*cp) - '0';
		cp++;
	}

	bd->bi_baudrate = rate * 100;
}

static void
rpx_cpuspeed(bd_t *bd, u_char *cp)
{
	uint	num, den;

	num = den = 0;

	while (*cp != '\n') {
		num *= 10;
		num += (*cp) - '0';
		cp++;
		if (*cp == '/') {
			cp++;
			den = (*cp) - '0';
			break;
		}
	}

	/* I don't know why the RPX just can't state the actual
	 * CPU speed.....
	 */
	if (den) {
		num /= den;
		num *= den;
	}
	bd->bi_intfreq = bd->bi_busfreq = num * 1000000;

	/* The 8xx can only run a maximum 50 MHz bus speed (until
	 * Motorola changes this :-).  Greater than 50 MHz parts
	 * run internal/2 for bus speed.
	 */
	if (num > 50)
		bd->bi_busfreq /= 2;
}
#endif

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC) || defined(CONFIG_EP405)
static void
rpx_memsize(bd_t *bd, u_char *cp)
{
	uint	size;

	size = 0;

	while (*cp != '\n') {
		size *= 10;
		size += (*cp) - '0';
		cp++;
	}

	bd->bi_memsize = size * 1024 * 1024;
}
#endif /* LITE || CLASSIC || EP405 */
#if defined(CONFIG_EP405)
static void
rpx_nvramsize(bd_t *bd, u_char *cp)
{
	uint	size;

	size = 0;

	while (*cp != '\n') {
		size *= 10;
		size += (*cp) - '0';
		cp++;
	}

	bd->bi_nvramsize = size * 1024;
}
#endif /* CONFIG_EP405 */

#endif	/* Embedded Planet boards */

#if defined(CONFIG_RPXLITE) || defined(CONFIG_RPXCLASSIC)

/* Read the EEPROM on the RPX-Lite board.
*/
void
embed_config(bd_t **bdp)
{
	u_char	eebuf[256], *cp;
	bd_t	*bd;

	/* Read the first 256 bytes of the EEPROM.  I think this
	 * is really all there is, and I hope if it gets bigger the
	 * info we want is still up front.
	 */
	bd = &bdinfo;
	*bdp = bd;

#if 1
	iic_read(0xa8, eebuf, 0, 128);
	iic_read(0xa8, &eebuf[128], 128, 128);

	/* We look for two things, the Ethernet address and the
	 * serial baud rate.  The records are separated by
	 * newlines.
	 */
	cp = eebuf;
	for (;;) {
		if (*cp == 'E') {
			cp++;
			if (*cp == 'A') {
				cp += 2;
				rpx_eth(bd, cp);
			}
		}
		if (*cp == 'S') {
			cp++;
			if (*cp == 'B') {
				cp += 2;
				rpx_brate(bd, cp);
			}
		}
		if (*cp == 'D') {
			cp++;
			if (*cp == '1') {
				cp += 2;
				rpx_memsize(bd, cp);
			}
		}
		if (*cp == 'H') {
			cp++;
			if (*cp == 'Z') {
				cp += 2;
				rpx_cpuspeed(bd, cp);
			}
		}

		/* Scan to the end of the record.
		*/
		while ((*cp != '\n') && (*cp != 0xff))
			cp++;

		/* If the next character is a 0 or ff, we are done.
		*/
		cp++;
		if ((*cp == 0) || (*cp == 0xff))
			break;
	}
	bd->bi_memstart = 0;
#else
	/* For boards without initialized EEPROM.
	*/
	bd->bi_memstart = 0;
	bd->bi_memsize = (8 * 1024 * 1024);
	bd->bi_intfreq = 48000000;
	bd->bi_busfreq = 48000000;
	bd->bi_baudrate = 9600;
#endif
}
#endif /* RPXLITE || RPXCLASSIC */

#ifdef CONFIG_BSEIP
/* Build a board information structure for the BSE ip-Engine.
 * There is more to come since we will add some environment
 * variables and a function to read them.
 */
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	bd = &bdinfo;
	*bdp = bd;

	/* Baud rate and processor speed will eventually come
	 * from the environment variables.
	 */
	bd->bi_baudrate = 9600;

	/* Get the Ethernet station address from the Flash ROM.
	*/
	cp = (u_char *)0xfe003ffa;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}

	/* The rest of this should come from the environment as well.
	*/
	bd->bi_memstart = 0;
	bd->bi_memsize = (16 * 1024 * 1024);
	bd->bi_intfreq = 48000000;
	bd->bi_busfreq = 48000000;
}
#endif /* BSEIP */

#ifdef CONFIG_FADS
/* Build a board information structure for the FADS.
 */
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	bd = &bdinfo;
	*bdp = bd;

	/* Just fill in some known values.
	 */
	bd->bi_baudrate = 9600;

	/* Use default enet.
	*/
	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}

	bd->bi_memstart = 0;
	bd->bi_memsize = (8 * 1024 * 1024);
	bd->bi_intfreq = 40000000;
	bd->bi_busfreq = 40000000;
}
#endif /* FADS */

#ifdef CONFIG_8260
/* Compute 8260 clock values if the rom doesn't provide them.
 */
static unsigned char bus2core_8260[] = {
/*      0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
	3,  2,  2,  2,  4,  4,  5,  9,  6, 11,  8, 10,  3, 12,  7,  2,
	6,  5, 13,  2, 14,  4, 15,  2,  3, 11,  8, 10, 16, 12,  7,  2,
};

static void
clk_8260(bd_t *bd)
{
	uint	scmr, vco_out, clkin;
	uint	plldf, pllmf, corecnf;
	volatile cpm2_map_t	*ip;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;
	scmr = ip->im_clkrst.car_scmr;

	/* The clkin is always bus frequency.
	*/
	clkin = bd->bi_busfreq;

	/* Collect the bits from the scmr.
	*/
	plldf = (scmr >> 12) & 1;
	pllmf = scmr & 0xfff;
	corecnf = (scmr >> 24) &0x1f;

	/* This is arithmetic from the 8260 manual.
	*/
	vco_out = clkin / (plldf + 1);
	vco_out *= 2 * (pllmf + 1);
	bd->bi_vco = vco_out;		/* Save for later */

	bd->bi_cpmfreq = vco_out / 2;	/* CPM Freq, in MHz */
	bd->bi_intfreq = bd->bi_busfreq * bus2core_8260[corecnf] / 2;

	/* Set Baud rate divisor.  The power up default is divide by 16,
	 * but we set it again here in case it was changed.
	 */
	ip->im_clkrst.car_sccr = 1;	/* DIV 16 BRG */
	bd->bi_brgfreq = vco_out / 16;
}

static unsigned char bus2core_8280[] = {
/*      0   1   2   3   4   5   6   7   8   9   a   b   c   d   e   f */
	3,  2,  2,  2,  4,  4,  5,  9,  6, 11,  8, 10,  3, 12,  7,  2,
	6,  5, 13,  2, 14,  2, 15,  2,  3,  2,  2,  2, 16,  2,  2,  2,
};

static void
clk_8280(bd_t *bd)
{
	uint	scmr, main_clk, clkin;
	uint	pllmf, corecnf;
	volatile cpm2_map_t	*ip;

	ip = (cpm2_map_t *)CPM_MAP_ADDR;
	scmr = ip->im_clkrst.car_scmr;

	/* The clkin is always bus frequency.
	*/
	clkin = bd->bi_busfreq;

	/* Collect the bits from the scmr.
	*/
	pllmf = scmr & 0xf;
	corecnf = (scmr >> 24) & 0x1f;

	/* This is arithmetic from the 8280 manual.
	*/
	main_clk = clkin * (pllmf + 1);

	bd->bi_cpmfreq = main_clk / 2;	/* CPM Freq, in MHz */
	bd->bi_intfreq = bd->bi_busfreq * bus2core_8280[corecnf] / 2;

	/* Set Baud rate divisor.  The power up default is divide by 16,
	 * but we set it again here in case it was changed.
	 */
	ip->im_clkrst.car_sccr = (ip->im_clkrst.car_sccr & 0x3) | 0x1;
	bd->bi_brgfreq = main_clk / 16;
}
#endif

#ifdef CONFIG_SBC82xx
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;
	unsigned long pvr;

	bd = *bdp;

	bd = &bdinfo;
	*bdp = bd;
	bd->bi_baudrate = 9600;
	bd->bi_memsize = 256 * 1024 * 1024;	/* just a guess */

	cp = (void*)SBC82xx_MACADDR_NVRAM_SCC1;
	memcpy(bd->bi_enetaddr, cp, 6);

	/* can busfreq be calculated? */
	pvr = mfspr(SPRN_PVR);
	if ((pvr & 0xffff0000) == 0x80820000) {
		bd->bi_busfreq = 100000000;
		clk_8280(bd);
	} else {
		bd->bi_busfreq = 66000000;
		clk_8260(bd);
	}

}
#endif /* SBC82xx */

#if defined(CONFIG_EST8260) || defined(CONFIG_TQM8260)
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	bd = *bdp;
#if 0
	/* This is actually provided by my boot rom.  I have it
	 * here for those people that may load the kernel with
	 * a JTAG/COP tool and not the rom monitor.
	 */
	bd->bi_baudrate = 115200;
	bd->bi_intfreq = 200000000;
	bd->bi_busfreq = 66666666;
	bd->bi_cpmfreq = 66666666;
	bd->bi_brgfreq = 33333333;
	bd->bi_memsize = 16 * 1024 * 1024;
#else
	/* The boot rom passes these to us in MHz.  Linux now expects
	 * them to be in Hz.
	 */
	bd->bi_intfreq *= 1000000;
	bd->bi_busfreq *= 1000000;
	bd->bi_cpmfreq *= 1000000;
	bd->bi_brgfreq *= 1000000;
#endif

	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}
}
#endif /* EST8260 */

#ifdef CONFIG_SBS8260
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	/* This should provided by the boot rom.
	 */
	bd = &bdinfo;
	*bdp = bd;
	bd->bi_baudrate = 9600;
	bd->bi_memsize = 64 * 1024 * 1024;

	/* Set all of the clocks.  We have to know the speed of the
	 * external clock.  The development board had 66 MHz.
	 */
	bd->bi_busfreq = 66666666;
	clk_8260(bd);

	/* I don't know how to compute this yet.
	*/
	bd->bi_intfreq = 133000000;


	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}
}
#endif /* SBS8260 */

#ifdef CONFIG_RPX8260
void
embed_config(bd_t **bdp)
{
	u_char	*cp, *keyvals;
	int	i;
	bd_t	*bd;

	keyvals = (u_char *)*bdp;

	bd = &bdinfo;
	*bdp = bd;

	/* This is almost identical to the RPX-Lite/Classic functions
	 * on the 8xx boards.  It would be nice to have a key lookup
	 * function in a string, but the format of all of the fields
	 * is slightly different.
	 */
	cp = keyvals;
	for (;;) {
		if (*cp == 'E') {
			cp++;
			if (*cp == 'A') {
				cp += 2;
				rpx_eth(bd, cp);
			}
		}
		if (*cp == 'S') {
			cp++;
			if (*cp == 'B') {
				cp += 2;
				bd->bi_baudrate = rpx_baseten(cp);
			}
		}
		if (*cp == 'D') {
			cp++;
			if (*cp == '1') {
				cp += 2;
				bd->bi_memsize = rpx_baseten(cp) * 1024 * 1024;
			}
		}
		if (*cp == 'X') {
			cp++;
			if (*cp == 'T') {
				cp += 2;
				bd->bi_busfreq = rpx_baseten(cp);
			}
		}
		if (*cp == 'N') {
			cp++;
			if (*cp == 'V') {
				cp += 2;
				bd->bi_nvsize = rpx_baseten(cp) * 1024 * 1024;
			}
		}

		/* Scan to the end of the record.
		*/
		while ((*cp != '\n') && (*cp != 0xff))
			cp++;

		/* If the next character is a 0 or ff, we are done.
		*/
		cp++;
		if ((*cp == 0) || (*cp == 0xff))
			break;
	}
	bd->bi_memstart = 0;

	/* The memory size includes both the 60x and local bus DRAM.
	 * I don't want to use the local bus DRAM for real memory,
	 * so subtract it out.  It would be nice if they were separate
	 * keys.
	 */
	bd->bi_memsize -= 32 * 1024 * 1024;

	/* Set all of the clocks.  We have to know the speed of the
	 * external clock.
	 */
	clk_8260(bd);

	/* I don't know how to compute this yet.
	*/
	bd->bi_intfreq = 200000000;
}
#endif /* RPX6 for testing */

#ifdef CONFIG_ADS8260
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	/* This should provided by the boot rom.
	 */
	bd = &bdinfo;
	*bdp = bd;
	bd->bi_baudrate = 9600;
	bd->bi_memsize = 16 * 1024 * 1024;

	/* Set all of the clocks.  We have to know the speed of the
	 * external clock.  The development board had 66 MHz.
	 */
	bd->bi_busfreq = 66666666;
	clk_8260(bd);

	/* I don't know how to compute this yet.
	*/
	bd->bi_intfreq = 200000000;


	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}
}
#endif /* ADS8260 */

#ifdef CONFIG_WILLOW
void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd;

	/* Willow has Open Firmware....I should learn how to get this
	 * information from it.
	 */
	bd = &bdinfo;
	*bdp = bd;
	bd->bi_baudrate = 9600;
	bd->bi_memsize = 32 * 1024 * 1024;

	/* Set all of the clocks.  We have to know the speed of the
	 * external clock.  The development board had 66 MHz.
	 */
	bd->bi_busfreq = 66666666;
	clk_8260(bd);

	/* I don't know how to compute this yet.
	*/
	bd->bi_intfreq = 200000000;


	cp = (u_char *)def_enet_addr;
	for (i=0; i<6; i++) {
		bd->bi_enetaddr[i] = *cp++;
	}
}
#endif /* WILLOW */

#ifdef CONFIG_XILINX_ML300
void
embed_config(bd_t ** bdp)
{
	static const unsigned long line_size = 32;
	static const unsigned long congruence_classes = 256;
	unsigned long addr;
	unsigned long dccr;
	bd_t *bd;

	/*
	 * Invalidate the data cache if the data cache is turned off.
	 * - The 405 core does not invalidate the data cache on power-up
	 *   or reset but does turn off the data cache. We cannot assume
	 *   that the cache contents are valid.
	 * - If the data cache is turned on this must have been done by
	 *   a bootloader and we assume that the cache contents are
	 *   valid.
	 */
	__asm__("mfdccr %0": "=r" (dccr));
	if (dccr == 0) {
		for (addr = 0;
		     addr < (congruence_classes * line_size);
		     addr += line_size) {
			__asm__("dccci 0,%0": :"b"(addr));
		}
	}

	bd = &bdinfo;
	*bdp = bd;
	bd->bi_memsize = XPAR_DDR_0_SIZE;
	bd->bi_intfreq = XPAR_CORE_CLOCK_FREQ_HZ;
	bd->bi_busfreq = XPAR_PLB_CLOCK_FREQ_HZ;
	bd->bi_pci_busfreq = XPAR_PCI_0_CLOCK_FREQ_HZ;
	timebase_period_ns = 1000000000 / bd->bi_tbfreq;
	/* see bi_tbfreq definition in arch/ppc/platforms/4xx/xilinx_ml300.h */
}
#endif /* CONFIG_XILINX_ML300 */

#ifdef CONFIG_IBM_OPENBIOS
/* This could possibly work for all treeboot roms.
*/
#if defined(CONFIG_BUBINGA)
#define BOARD_INFO_VECTOR       0xFFF80B50 /* openbios 1.19 moved this vector down  - armin */
#else
#define BOARD_INFO_VECTOR	0xFFFE0B50
#endif

void
embed_config(bd_t **bdp)
{
	u_char	*cp;
	int	i;
	bd_t	*bd, *treeboot_bd;
	bd_t *(*get_board_info)(void) =
	    (bd_t *(*)(void))(*(unsigned long *)BOARD_INFO_VECTOR);
#if !defined(CONFIG_STB03xxx)

	/* shut down the Ethernet controller that the boot rom
	 * sometimes leaves running.
	 */
	mtdcr(DCRN_MALCR(DCRN_MAL_BASE), MALCR_MMSR);     /* 1st reset MAL */
	while (mfdcr(DCRN_MALCR(DCRN_MAL_BASE)) & MALCR_MMSR) {}; /* wait for the reset */	
	out_be32((volatile u32*)EMAC0_BASE,0x20000000);        /* then reset EMAC */
#endif

	bd = &bdinfo;
	*bdp = bd;
	if ((treeboot_bd = get_board_info()) != NULL) {
		memcpy(bd, treeboot_bd, sizeof(bd_t));
	}
	else {
		/* Hmmm...better try to stuff some defaults.
		*/
		bd->bi_memsize = 16 * 1024 * 1024;
		cp = (u_char *)def_enet_addr;
		for (i=0; i<6; i++) {
			/* I should probably put different ones here,
			 * hopefully only one is used.
			 */
			bd->BD_EMAC_ADDR(0,i) = *cp;

#ifdef CONFIG_PCI
			bd->bi_pci_enetaddr[i] = *cp++;
#endif
		}
		bd->bi_tbfreq = 200 * 1000 * 1000;
		bd->bi_intfreq = 200000000;
		bd->bi_busfreq = 100000000;
#ifdef CONFIG_PCI
		bd->bi_pci_busfreq = 66666666;
#endif
	}
	/* Yeah, this look weird, but on Redwood 4 they are
	 * different object in the structure.  Sincr Redwwood 5
	 * and Redwood 6 use OpenBIOS, it requires a special value.
	 */
#if defined(CONFIG_REDWOOD_5) || defined (CONFIG_REDWOOD_6)
	bd->bi_tbfreq = 27 * 1000 * 1000;
#endif
	timebase_period_ns = 1000000000 / bd->bi_tbfreq;
}
#endif /* CONFIG_IBM_OPENBIOS */

#ifdef CONFIG_EP405
#include <linux/serial_reg.h>

void
embed_config(bd_t **bdp)
{
	u32 chcr0;
	u_char *cp;
	bd_t	*bd;

	/* Different versions of the PlanetCore firmware vary in how
	   they set up the serial port - in particular whether they
	   use the internal or external serial clock for UART0.  Make
	   sure the UART is in a known state. */
	/* FIXME: We should use the board's 11.0592MHz external serial
	   clock - it will be more accurate for serial rates.  For
	   now, however the baud rates in ep405.h are for the internal
	   clock. */
	chcr0 = mfdcr(DCRN_CHCR0);
	if ( (chcr0 & 0x1fff) != 0x103e ) {
		mtdcr(DCRN_CHCR0, (chcr0 & 0xffffe000) | 0x103e);
		/* The following tricks serial_init() into resetting the baud rate */
		writeb(0, UART0_IO_BASE + UART_LCR);
	}

	/* We haven't seen actual problems with the EP405 leaving the
	 * EMAC running (as we have on Walnut).  But the registers
	 * suggest it may not be left completely quiescent.  Reset it
	 * just to be sure. */
	mtdcr(DCRN_MALCR(DCRN_MAL_BASE), MALCR_MMSR);     /* 1st reset MAL */
	while (mfdcr(DCRN_MALCR(DCRN_MAL_BASE)) & MALCR_MMSR) {}; /* wait for the reset */	
	out_be32((unsigned *)EMAC0_BASE,0x20000000);        /* then reset EMAC */

	bd = &bdinfo;
	*bdp = bd;
#if 1
	        cp = (u_char *)0xF0000EE0;
	        for (;;) {
	                if (*cp == 'E') {
	                        cp++;
	                        if (*cp == 'A') {
                                  cp += 2;
                                  rpx_eth(bd, cp);
	                        }
		         }

	         	if (*cp == 'D') {
	                        	cp++;
	                        	if (*cp == '1') {
		                                cp += 2;
		                                rpx_memsize(bd, cp);
	        	                }
                	}

			if (*cp == 'N') {
				cp++;
				if (*cp == 'V') {
					cp += 2;
					rpx_nvramsize(bd, cp);
				}
			}
			while ((*cp != '\n') && (*cp != 0xff))
			      cp++;

	                cp++;
	                if ((*cp == 0) || (*cp == 0xff))
	                   break;
	       }
	bd->bi_intfreq   = 200000000;
	bd->bi_busfreq   = 100000000;
	bd->bi_pci_busfreq= 33000000 ;
#else

	bd->bi_memsize   = 64000000;
	bd->bi_intfreq   = 200000000;
	bd->bi_busfreq   = 100000000;
	bd->bi_pci_busfreq= 33000000 ;
#endif
}
#endif
