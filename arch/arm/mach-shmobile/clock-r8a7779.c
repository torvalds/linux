/*
 * r8a7779 clock framework support
 *
 * Copyright (C) 2011  Renesas Solutions Corp.
 * Copyright (C) 2011  Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
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
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/sh_clk.h>
#include <linux/clkdev.h>
#include <linux/sh_timer.h>
#include <mach/r8a7779.h>
#include "clock.h"
#include "common.h"

/*
 *		MD1 = 1			MD1 = 0
 *		(PLLA = 1500)		(PLLA = 1600)
 *		(MHz)			(MHz)
 *------------------------------------------------+--------------------
 * clkz		1000   (2/3)		800   (1/2)
 * clkzs	 250   (1/6)		200   (1/8)
 * clki		 750   (1/2)		800   (1/2)
 * clks		 250   (1/6)		200   (1/8)
 * clks1	 125   (1/12)		100   (1/16)
 * clks3	 187.5 (1/8)		200   (1/8)
 * clks4	  93.7 (1/16)		100   (1/16)
 * clkp		  62.5 (1/24)		 50   (1/32)
 * clkg		  62.5 (1/24)		 66.6 (1/24)
 * clkb, CLKOUT
 * (MD2 = 0)	  62.5 (1/24)		 66.6 (1/24)
 * (MD2 = 1)	  41.6 (1/36)		 50   (1/32)
*/

#define MD(nr)	BIT(nr)

#define MSTPCR0		IOMEM(0xffc80030)
#define MSTPCR1		IOMEM(0xffc80034)
#define MSTPCR3		IOMEM(0xffc8003c)
#define MSTPSR1		IOMEM(0xffc80044)

/* ioremap() through clock mapping mandatory to avoid
 * collision with ARM coherent DMA virtual memory range.
 */

static struct clk_mapping cpg_mapping = {
	.phys	= 0xffc80000,
	.len	= 0x80,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk plla_clk = {
	/* .rate will be updated on r8a7779_clock_init() */
	.mapping	= &cpg_mapping,
};

/*
 * clock ratio of these clock will be updated
 * on r8a7779_clock_init()
 */
SH_FIXED_RATIO_CLK_SET(clkz_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clkzs_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clki_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clks_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clks1_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clks3_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clks4_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clkb_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clkout_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clkp_clk,	plla_clk, 1, 1);
SH_FIXED_RATIO_CLK_SET(clkg_clk,	plla_clk, 1, 1);

static struct clk *main_clks[] = {
	&plla_clk,
	&clkz_clk,
	&clkzs_clk,
	&clki_clk,
	&clks_clk,
	&clks1_clk,
	&clks3_clk,
	&clks4_clk,
	&clkb_clk,
	&clkout_clk,
	&clkp_clk,
	&clkg_clk,
};

enum { MSTP323, MSTP322, MSTP321, MSTP320,
	MSTP120,
	MSTP116, MSTP115, MSTP114,
	MSTP110, MSTP109, MSTP108,
	MSTP103, MSTP101, MSTP100,
	MSTP030,
	MSTP029, MSTP028, MSTP027, MSTP026, MSTP025, MSTP024, MSTP023, MSTP022, MSTP021,
	MSTP016, MSTP015, MSTP014,
	MSTP007,
	MSTP_NR };

static struct clk mstp_clks[MSTP_NR] = {
	[MSTP323] = SH_CLK_MSTP32(&clkp_clk, MSTPCR3, 23, 0), /* SDHI0 */
	[MSTP322] = SH_CLK_MSTP32(&clkp_clk, MSTPCR3, 22, 0), /* SDHI1 */
	[MSTP321] = SH_CLK_MSTP32(&clkp_clk, MSTPCR3, 21, 0), /* SDHI2 */
	[MSTP320] = SH_CLK_MSTP32(&clkp_clk, MSTPCR3, 20, 0), /* SDHI3 */
	[MSTP120] = SH_CLK_MSTP32_STS(&clks_clk, MSTPCR1, 20, MSTPSR1, 0), /* VIN3 */
	[MSTP116] = SH_CLK_MSTP32_STS(&clkp_clk, MSTPCR1, 16, MSTPSR1, 0), /* PCIe */
	[MSTP115] = SH_CLK_MSTP32_STS(&clkp_clk, MSTPCR1, 15, MSTPSR1, 0), /* SATA */
	[MSTP114] = SH_CLK_MSTP32_STS(&clkp_clk, MSTPCR1, 14, MSTPSR1, 0), /* Ether */
	[MSTP110] = SH_CLK_MSTP32_STS(&clks_clk, MSTPCR1, 10, MSTPSR1, 0), /* VIN0 */
	[MSTP109] = SH_CLK_MSTP32_STS(&clks_clk, MSTPCR1,  9, MSTPSR1, 0), /* VIN1 */
	[MSTP108] = SH_CLK_MSTP32_STS(&clks_clk, MSTPCR1,  8, MSTPSR1, 0), /* VIN2 */
	[MSTP103] = SH_CLK_MSTP32_STS(&clks_clk, MSTPCR1,  3, MSTPSR1, 0), /* DU */
	[MSTP101] = SH_CLK_MSTP32_STS(&clkp_clk, MSTPCR1,  1, MSTPSR1, 0), /* USB2 */
	[MSTP100] = SH_CLK_MSTP32_STS(&clkp_clk, MSTPCR1,  0, MSTPSR1, 0), /* USB0/1 */
	[MSTP030] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 30, 0), /* I2C0 */
	[MSTP029] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 29, 0), /* I2C1 */
	[MSTP028] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 28, 0), /* I2C2 */
	[MSTP027] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 27, 0), /* I2C3 */
	[MSTP026] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 26, 0), /* SCIF0 */
	[MSTP025] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 25, 0), /* SCIF1 */
	[MSTP024] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 24, 0), /* SCIF2 */
	[MSTP023] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 23, 0), /* SCIF3 */
	[MSTP022] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 22, 0), /* SCIF4 */
	[MSTP021] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 21, 0), /* SCIF5 */
	[MSTP016] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 16, 0), /* TMU0 */
	[MSTP015] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 15, 0), /* TMU1 */
	[MSTP014] = SH_CLK_MSTP32(&clkp_clk, MSTPCR0, 14, 0), /* TMU2 */
	[MSTP007] = SH_CLK_MSTP32(&clks_clk, MSTPCR0,  7, 0), /* HSPI */
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("plla_clk", &plla_clk),
	CLKDEV_CON_ID("clkz_clk", &clkz_clk),
	CLKDEV_CON_ID("clkzs_clk", &clkzs_clk),

	/* DIV4 clocks */
	CLKDEV_CON_ID("shyway_clk",	&clks_clk),
	CLKDEV_CON_ID("bus_clk",	&clkout_clk),
	CLKDEV_CON_ID("shyway4_clk",	&clks4_clk),
	CLKDEV_CON_ID("shyway3_clk",	&clks3_clk),
	CLKDEV_CON_ID("shyway1_clk",	&clks1_clk),
	CLKDEV_CON_ID("peripheral_clk",	&clkp_clk),

	/* MSTP32 clocks */
	CLKDEV_DEV_ID("r8a7779-vin.3", &mstp_clks[MSTP120]), /* VIN3 */
	CLKDEV_DEV_ID("rcar-pcie", &mstp_clks[MSTP116]), /* PCIe */
	CLKDEV_DEV_ID("sata_rcar", &mstp_clks[MSTP115]), /* SATA */
	CLKDEV_DEV_ID("fc600000.sata", &mstp_clks[MSTP115]), /* SATA w/DT */
	CLKDEV_DEV_ID("r8a777x-ether", &mstp_clks[MSTP114]), /* Ether */
	CLKDEV_DEV_ID("r8a7779-vin.0", &mstp_clks[MSTP110]), /* VIN0 */
	CLKDEV_DEV_ID("r8a7779-vin.1", &mstp_clks[MSTP109]), /* VIN1 */
	CLKDEV_DEV_ID("r8a7779-vin.2", &mstp_clks[MSTP108]), /* VIN2 */
	CLKDEV_DEV_ID("ehci-platform.1", &mstp_clks[MSTP101]), /* USB EHCI port2 */
	CLKDEV_DEV_ID("ohci-platform.1", &mstp_clks[MSTP101]), /* USB OHCI port2 */
	CLKDEV_DEV_ID("ehci-platform.0", &mstp_clks[MSTP100]), /* USB EHCI port0/1 */
	CLKDEV_DEV_ID("ohci-platform.0", &mstp_clks[MSTP100]), /* USB OHCI port0/1 */
	CLKDEV_ICK_ID("fck", "sh-tmu.0", &mstp_clks[MSTP016]), /* TMU0 */
	CLKDEV_DEV_ID("i2c-rcar.0", &mstp_clks[MSTP030]), /* I2C0 */
	CLKDEV_DEV_ID("ffc70000.i2c", &mstp_clks[MSTP030]), /* I2C0 */
	CLKDEV_DEV_ID("i2c-rcar.1", &mstp_clks[MSTP029]), /* I2C1 */
	CLKDEV_DEV_ID("ffc71000.i2c", &mstp_clks[MSTP029]), /* I2C1 */
	CLKDEV_DEV_ID("i2c-rcar.2", &mstp_clks[MSTP028]), /* I2C2 */
	CLKDEV_DEV_ID("ffc72000.i2c", &mstp_clks[MSTP028]), /* I2C2 */
	CLKDEV_DEV_ID("i2c-rcar.3", &mstp_clks[MSTP027]), /* I2C3 */
	CLKDEV_DEV_ID("ffc73000.i2c", &mstp_clks[MSTP027]), /* I2C3 */
	CLKDEV_DEV_ID("sh-sci.0", &mstp_clks[MSTP026]), /* SCIF0 */
	CLKDEV_DEV_ID("sh-sci.1", &mstp_clks[MSTP025]), /* SCIF1 */
	CLKDEV_DEV_ID("sh-sci.2", &mstp_clks[MSTP024]), /* SCIF2 */
	CLKDEV_DEV_ID("sh-sci.3", &mstp_clks[MSTP023]), /* SCIF3 */
	CLKDEV_DEV_ID("sh-sci.4", &mstp_clks[MSTP022]), /* SCIF4 */
	CLKDEV_DEV_ID("sh-sci.5", &mstp_clks[MSTP021]), /* SCIF6 */
	CLKDEV_DEV_ID("sh-hspi.0", &mstp_clks[MSTP007]), /* HSPI0 */
	CLKDEV_DEV_ID("fffc7000.spi", &mstp_clks[MSTP007]), /* HSPI0 */
	CLKDEV_DEV_ID("sh-hspi.1", &mstp_clks[MSTP007]), /* HSPI1 */
	CLKDEV_DEV_ID("fffc8000.spi", &mstp_clks[MSTP007]), /* HSPI1 */
	CLKDEV_DEV_ID("sh-hspi.2", &mstp_clks[MSTP007]), /* HSPI2 */
	CLKDEV_DEV_ID("fffc6000.spi", &mstp_clks[MSTP007]), /* HSPI2 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.0", &mstp_clks[MSTP323]), /* SDHI0 */
	CLKDEV_DEV_ID("ffe4c000.sd", &mstp_clks[MSTP323]), /* SDHI0 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.1", &mstp_clks[MSTP322]), /* SDHI1 */
	CLKDEV_DEV_ID("ffe4d000.sd", &mstp_clks[MSTP322]), /* SDHI1 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.2", &mstp_clks[MSTP321]), /* SDHI2 */
	CLKDEV_DEV_ID("ffe4e000.sd", &mstp_clks[MSTP321]), /* SDHI2 */
	CLKDEV_DEV_ID("sh_mobile_sdhi.3", &mstp_clks[MSTP320]), /* SDHI3 */
	CLKDEV_DEV_ID("ffe4f000.sd", &mstp_clks[MSTP320]), /* SDHI3 */
	CLKDEV_DEV_ID("rcar-du-r8a7779", &mstp_clks[MSTP103]), /* DU */
};

void __init r8a7779_clock_init(void)
{
	u32 mode = r8a7779_read_mode_pins();
	int k, ret = 0;

	if (mode & MD(1)) {
		plla_clk.rate = 1500000000;

		SH_CLK_SET_RATIO(&clkz_clk_ratio,	2, 3);
		SH_CLK_SET_RATIO(&clkzs_clk_ratio,	1, 6);
		SH_CLK_SET_RATIO(&clki_clk_ratio,	1, 2);
		SH_CLK_SET_RATIO(&clks_clk_ratio,	1, 6);
		SH_CLK_SET_RATIO(&clks1_clk_ratio,	1, 12);
		SH_CLK_SET_RATIO(&clks3_clk_ratio,	1, 8);
		SH_CLK_SET_RATIO(&clks4_clk_ratio,	1, 16);
		SH_CLK_SET_RATIO(&clkp_clk_ratio,	1, 24);
		SH_CLK_SET_RATIO(&clkg_clk_ratio,	1, 24);
		if (mode & MD(2)) {
			SH_CLK_SET_RATIO(&clkb_clk_ratio,	1, 36);
			SH_CLK_SET_RATIO(&clkout_clk_ratio,	1, 36);
		} else {
			SH_CLK_SET_RATIO(&clkb_clk_ratio,	1, 24);
			SH_CLK_SET_RATIO(&clkout_clk_ratio,	1, 24);
		}
	} else {
		plla_clk.rate = 1600000000;

		SH_CLK_SET_RATIO(&clkz_clk_ratio,	1, 2);
		SH_CLK_SET_RATIO(&clkzs_clk_ratio,	1, 8);
		SH_CLK_SET_RATIO(&clki_clk_ratio,	1, 2);
		SH_CLK_SET_RATIO(&clks_clk_ratio,	1, 8);
		SH_CLK_SET_RATIO(&clks1_clk_ratio,	1, 16);
		SH_CLK_SET_RATIO(&clks3_clk_ratio,	1, 8);
		SH_CLK_SET_RATIO(&clks4_clk_ratio,	1, 16);
		SH_CLK_SET_RATIO(&clkp_clk_ratio,	1, 32);
		SH_CLK_SET_RATIO(&clkg_clk_ratio,	1, 24);
		if (mode & MD(2)) {
			SH_CLK_SET_RATIO(&clkb_clk_ratio,	1, 32);
			SH_CLK_SET_RATIO(&clkout_clk_ratio,	1, 32);
		} else {
			SH_CLK_SET_RATIO(&clkb_clk_ratio,	1, 24);
			SH_CLK_SET_RATIO(&clkout_clk_ratio,	1, 24);
		}
	}

	for (k = 0; !ret && (k < ARRAY_SIZE(main_clks)); k++)
		ret = clk_register(main_clks[k]);

	if (!ret)
		ret = sh_clk_mstp_register(mstp_clks, MSTP_NR);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	if (!ret)
		shmobile_clk_init();
	else
		panic("failed to setup r8a7779 clocks\n");
}

/* do nothing for !CONFIG_SMP or !CONFIG_HAVE_TWD */
void __init __weak r8a7779_register_twd(void) { }

void __init r8a7779_earlytimer_init(void)
{
	r8a7779_clock_init();
	r8a7779_register_twd();
	shmobile_earlytimer_init();
}
