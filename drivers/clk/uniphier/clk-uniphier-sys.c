// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Socionext Inc.
 *   Author: Masahiro Yamada <yamada.masahiro@socionext.com>
 */

#include <linux/stddef.h>

#include "clk-uniphier.h"

#define UNIPHIER_LD4_SYS_CLK_SD					\
	UNIPHIER_CLK_FACTOR("sd-200m", -1, "spll", 1, 8),		\
	UNIPHIER_CLK_FACTOR("sd-133m", -1, "vpll27a", 1, 2)

#define UNIPHIER_PRO5_SYS_CLK_SD					\
	UNIPHIER_CLK_FACTOR("sd-200m", -1, "spll", 1, 12),		\
	UNIPHIER_CLK_FACTOR("sd-133m", -1, "spll", 1, 18)

#define UNIPHIER_LD20_SYS_CLK_SD					\
	UNIPHIER_CLK_FACTOR("sd-200m", -1, "spll", 1, 10),		\
	UNIPHIER_CLK_FACTOR("sd-133m", -1, "spll", 1, 15)

#define UNIPHIER_NX1_SYS_CLK_SD						\
	UNIPHIER_CLK_FACTOR("sd-200m", -1, "spll", 1, 4),		\
	UNIPHIER_CLK_FACTOR("sd-133m", -1, "spll", 1, 6)

#define UNIPHIER_LD4_SYS_CLK_NAND(idx)					\
	UNIPHIER_CLK_FACTOR("nand-50m", -1, "spll", 1, 32),		\
	UNIPHIER_CLK_GATE("nand", (idx), "nand-50m", 0x2104, 2)

#define UNIPHIER_PRO5_SYS_CLK_NAND(idx)					\
	UNIPHIER_CLK_FACTOR("nand-50m", -1, "spll", 1, 48),		\
	UNIPHIER_CLK_GATE("nand", (idx), "nand-50m", 0x2104, 2)

#define UNIPHIER_LD11_SYS_CLK_NAND(idx)					\
	UNIPHIER_CLK_FACTOR("nand-50m", -1, "spll", 1, 40),		\
	UNIPHIER_CLK_GATE("nand", (idx), "nand-50m", 0x210c, 0)

#define UNIPHIER_SYS_CLK_NAND_4X(idx)					\
	UNIPHIER_CLK_FACTOR("nand-4x", (idx), "nand", 4, 1)

#define UNIPHIER_LD11_SYS_CLK_EMMC(idx)					\
	UNIPHIER_CLK_GATE("emmc", (idx), NULL, 0x210c, 2)

#define UNIPHIER_LD4_SYS_CLK_STDMAC(idx)				\
	UNIPHIER_CLK_GATE("stdmac", (idx), NULL, 0x2104, 10)

#define UNIPHIER_LD11_SYS_CLK_STDMAC(idx)				\
	UNIPHIER_CLK_GATE("stdmac", (idx), NULL, 0x210c, 8)

#define UNIPHIER_LD11_SYS_CLK_HSC(idx)					\
	UNIPHIER_CLK_GATE("hsc", (idx), NULL, 0x210c, 9)

#define UNIPHIER_PRO4_SYS_CLK_GIO(idx)					\
	UNIPHIER_CLK_GATE("gio", (idx), NULL, 0x2104, 6)

#define UNIPHIER_PRO4_SYS_CLK_USB3(idx, ch)				\
	UNIPHIER_CLK_GATE("usb3" #ch, (idx), NULL, 0x2104, 16 + (ch))

#define UNIPHIER_PRO4_SYS_CLK_AIO(idx)					\
	UNIPHIER_CLK_FACTOR("aio-io200m", -1, "spll", 1, 8),		\
	UNIPHIER_CLK_GATE("aio", (idx), "aio-io200m", 0x2104, 13)

#define UNIPHIER_PRO5_SYS_CLK_AIO(idx)					\
	UNIPHIER_CLK_FACTOR("aio-io200m", -1, "spll", 1, 12),		\
	UNIPHIER_CLK_GATE("aio", (idx), "aio-io200m", 0x2104, 13)

#define UNIPHIER_LD11_SYS_CLK_AIO(idx)					\
	UNIPHIER_CLK_FACTOR("aio-io200m", -1, "spll", 1, 10),		\
	UNIPHIER_CLK_GATE("aio", (idx), "aio-io200m", 0x2108, 0)

#define UNIPHIER_LD11_SYS_CLK_EVEA(idx)					\
	UNIPHIER_CLK_FACTOR("evea-io100m", -1, "spll", 1, 20),		\
	UNIPHIER_CLK_GATE("evea", (idx), "evea-io100m", 0x2108, 1)

#define UNIPHIER_LD11_SYS_CLK_EXIV(idx)					\
	UNIPHIER_CLK_FACTOR("exiv-io200m", -1, "spll", 1, 10),		\
	UNIPHIER_CLK_GATE("exiv", (idx), "exiv-io200m", 0x2110, 2)

#define UNIPHIER_PRO4_SYS_CLK_ETHER(idx)				\
	UNIPHIER_CLK_GATE("ether", (idx), NULL, 0x2104, 12)

#define UNIPHIER_LD11_SYS_CLK_ETHER(idx)				\
	UNIPHIER_CLK_GATE("ether", (idx), NULL, 0x210c, 6)

const struct uniphier_clk_data uniphier_ld4_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 65, 1),		/* 1597.44 MHz */
	UNIPHIER_CLK_FACTOR("upll", -1, "ref", 6000, 512),	/* 288 MHz */
	UNIPHIER_CLK_FACTOR("a2pll", -1, "ref", 24, 1),		/* 589.824 MHz */
	UNIPHIER_CLK_FACTOR("vpll27a", -1, "ref", 5625, 512),	/* 270 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "a2pll", 1, 16),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 16),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 32),
	UNIPHIER_LD4_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD4_SYS_CLK_SD,
	UNIPHIER_CLK_FACTOR("usb2", -1, "upll", 1, 12),
	UNIPHIER_LD4_SYS_CLK_STDMAC(8),			/* Ether, HSC, MIO */
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pro4_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 64, 1),		/* 1600 MHz */
	UNIPHIER_CLK_FACTOR("upll", -1, "ref", 288, 25),	/* 288 MHz */
	UNIPHIER_CLK_FACTOR("a2pll", -1, "upll", 256, 125),	/* 589.824 MHz */
	UNIPHIER_CLK_FACTOR("vpll27a", -1, "ref", 270, 25),	/* 270 MHz */
	UNIPHIER_CLK_FACTOR("gpll", -1, "ref", 10, 1),		/* 250 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "a2pll", 1, 8),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 32),
	UNIPHIER_CLK_FACTOR("spi", 1, "spll", 1, 32),
	UNIPHIER_LD4_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD4_SYS_CLK_SD,
	UNIPHIER_CLK_FACTOR("usb2", -1, "upll", 1, 12),
	UNIPHIER_PRO4_SYS_CLK_ETHER(6),
	UNIPHIER_CLK_GATE("ether-gb", 7, "gpll", 0x2104, 5),
	UNIPHIER_LD4_SYS_CLK_STDMAC(8),			/* HSC, MIO, RLE */
	UNIPHIER_CLK_GATE("ether-phy", 10, "ref", 0x2260, 0),
	UNIPHIER_PRO4_SYS_CLK_GIO(12),			/* Ether, SATA, USB3 */
	UNIPHIER_PRO4_SYS_CLK_USB3(14, 0),
	UNIPHIER_PRO4_SYS_CLK_USB3(15, 1),
	UNIPHIER_CLK_FACTOR("usb30-hsphy0", 16, "upll", 1, 12),
	UNIPHIER_CLK_FACTOR("usb30-ssphy0", 17, "ref", 1, 1),
	UNIPHIER_CLK_FACTOR("usb31-ssphy0", 20, "ref", 1, 1),
	UNIPHIER_CLK_GATE("sata0", 28, NULL, 0x2104, 18),
	UNIPHIER_CLK_GATE("sata1", 29, NULL, 0x2104, 19),
	UNIPHIER_PRO4_SYS_CLK_AIO(40),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_sld8_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 64, 1),		/* 1600 MHz */
	UNIPHIER_CLK_FACTOR("upll", -1, "ref", 288, 25),	/* 288 MHz */
	UNIPHIER_CLK_FACTOR("vpll27a", -1, "ref", 270, 25),	/* 270 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 20),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 16),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 32),
	UNIPHIER_LD4_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD4_SYS_CLK_SD,
	UNIPHIER_CLK_FACTOR("usb2", -1, "upll", 1, 12),
	UNIPHIER_LD4_SYS_CLK_STDMAC(8),			/* Ether, HSC, MIO */
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pro5_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 120, 1),		/* 2400 MHz */
	UNIPHIER_CLK_FACTOR("dapll1", -1, "ref", 128, 1),	/* 2560 MHz */
	UNIPHIER_CLK_FACTOR("dapll2", -1, "dapll1", 144, 125),	/* 2949.12 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "dapll2", 1, 40),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 48),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 48),
	UNIPHIER_PRO5_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_PRO5_SYS_CLK_SD,
	UNIPHIER_LD4_SYS_CLK_STDMAC(8),				/* HSC */
	UNIPHIER_PRO4_SYS_CLK_GIO(12),				/* PCIe, USB3 */
	UNIPHIER_PRO4_SYS_CLK_USB3(14, 0),
	UNIPHIER_PRO4_SYS_CLK_USB3(15, 1),
	UNIPHIER_CLK_GATE("pcie", 24, NULL, 0x2108, 2),
	UNIPHIER_PRO5_SYS_CLK_AIO(40),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pxs2_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 96, 1),		/* 2400 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 27),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 48),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 48),
	UNIPHIER_PRO5_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_PRO5_SYS_CLK_SD,
	UNIPHIER_PRO4_SYS_CLK_ETHER(6),
	UNIPHIER_LD4_SYS_CLK_STDMAC(8),				/* HSC, RLE */
	/* GIO is always clock-enabled: no function for 0x2104 bit6 */
	UNIPHIER_PRO4_SYS_CLK_USB3(14, 0),
	UNIPHIER_PRO4_SYS_CLK_USB3(15, 1),
	/* The document mentions 0x2104 bit 18, but not functional */
	UNIPHIER_CLK_GATE("usb30-hsphy0", 16, NULL, 0x2104, 19),
	UNIPHIER_CLK_FACTOR("usb30-ssphy0", 17, "ref", 1, 1),
	UNIPHIER_CLK_FACTOR("usb30-ssphy1", 18, "ref", 1, 1),
	UNIPHIER_CLK_GATE("usb31-hsphy0", 20, NULL, 0x2104, 20),
	UNIPHIER_CLK_FACTOR("usb31-ssphy0", 21, "ref", 1, 1),
	UNIPHIER_CLK_GATE("sata0", 28, NULL, 0x2104, 22),
	UNIPHIER_PRO5_SYS_CLK_AIO(40),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_ld11_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("cpll", -1, "ref", 392, 5),		/* 1960 MHz */
	UNIPHIER_CLK_FACTOR("mpll", -1, "ref", 64, 1),		/* 1600 MHz */
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 80, 1),		/* 2000 MHz */
	UNIPHIER_CLK_FACTOR("vspll", -1, "ref", 80, 1),		/* 2000 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 34),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 40),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 40),
	UNIPHIER_LD11_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD11_SYS_CLK_EMMC(4),
	/* Index 5 reserved for eMMC PHY */
	UNIPHIER_LD11_SYS_CLK_ETHER(6),
	UNIPHIER_LD11_SYS_CLK_STDMAC(8),			/* HSC, MIO */
	UNIPHIER_LD11_SYS_CLK_HSC(9),
	UNIPHIER_CLK_FACTOR("usb2", -1, "ref", 24, 25),
	UNIPHIER_LD11_SYS_CLK_AIO(40),
	UNIPHIER_LD11_SYS_CLK_EVEA(41),
	UNIPHIER_LD11_SYS_CLK_EXIV(42),
	/* CPU gears */
	UNIPHIER_CLK_DIV4("cpll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV4("mpll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV3("spll", 3, 4, 8),
	/* Note: both gear1 and gear4 are spll/4.  This is not a bug. */
	UNIPHIER_CLK_CPUGEAR("cpu-ca53", 33, 0x8080, 0xf, 8,
			     "cpll/2", "spll/4", "cpll/3", "spll/3",
			     "spll/4", "spll/8", "cpll/4", "cpll/8"),
	UNIPHIER_CLK_CPUGEAR("cpu-ipp", 34, 0x8100, 0xf, 8,
			     "mpll/2", "spll/4", "mpll/3", "spll/3",
			     "spll/4", "spll/8", "mpll/4", "mpll/8"),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_ld20_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("cpll", -1, "ref", 88, 1),		/* ARM: 2200 MHz */
	UNIPHIER_CLK_FACTOR("gppll", -1, "ref", 52, 1),		/* Mali: 1300 MHz */
	UNIPHIER_CLK_FACTOR("mpll", -1, "ref", 64, 1),		/* Codec: 1600 MHz */
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 80, 1),		/* 2000 MHz */
	UNIPHIER_CLK_FACTOR("s2pll", -1, "ref", 88, 1),		/* IPP: 2200 MHz */
	UNIPHIER_CLK_FACTOR("vppll", -1, "ref", 504, 5),	/* 2520 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 34),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 40),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 40),
	UNIPHIER_LD11_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD11_SYS_CLK_EMMC(4),
	/* Index 5 reserved for eMMC PHY */
	UNIPHIER_LD20_SYS_CLK_SD,
	UNIPHIER_LD11_SYS_CLK_ETHER(6),
	UNIPHIER_LD11_SYS_CLK_STDMAC(8),			/* HSC */
	UNIPHIER_LD11_SYS_CLK_HSC(9),
	/* GIO is always clock-enabled: no function for 0x210c bit5 */
	/*
	 * clock for USB Link is enabled by the logic "OR" of bit 14 and bit 15.
	 * We do not use bit 15 here.
	 */
	UNIPHIER_CLK_GATE("usb30", 14, NULL, 0x210c, 14),
	UNIPHIER_CLK_GATE("usb30-hsphy0", 16, NULL, 0x210c, 12),
	UNIPHIER_CLK_GATE("usb30-hsphy1", 17, NULL, 0x210c, 13),
	UNIPHIER_CLK_FACTOR("usb30-ssphy0", 18, "ref", 1, 1),
	UNIPHIER_CLK_FACTOR("usb30-ssphy1", 19, "ref", 1, 1),
	UNIPHIER_CLK_GATE("pcie", 24, NULL, 0x210c, 4),
	UNIPHIER_LD11_SYS_CLK_AIO(40),
	UNIPHIER_LD11_SYS_CLK_EVEA(41),
	UNIPHIER_LD11_SYS_CLK_EXIV(42),
	/* CPU gears */
	UNIPHIER_CLK_DIV4("cpll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV4("spll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV4("s2pll", 2, 3, 4, 8),
	UNIPHIER_CLK_CPUGEAR("cpu-ca72", 32, 0x8000, 0xf, 8,
			     "cpll/2", "spll/2", "cpll/3", "spll/3",
			     "spll/4", "spll/8", "cpll/4", "cpll/8"),
	UNIPHIER_CLK_CPUGEAR("cpu-ca53", 33, 0x8080, 0xf, 8,
			     "cpll/2", "spll/2", "cpll/3", "spll/3",
			     "spll/4", "spll/8", "cpll/4", "cpll/8"),
	UNIPHIER_CLK_CPUGEAR("cpu-ipp", 34, 0x8100, 0xf, 8,
			     "s2pll/2", "spll/2", "s2pll/3", "spll/3",
			     "spll/4", "spll/8", "s2pll/4", "s2pll/8"),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pxs3_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("cpll", -1, "ref", 104, 1),		/* ARM: 2600 MHz */
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 80, 1),		/* 2000 MHz */
	UNIPHIER_CLK_FACTOR("s2pll", -1, "ref", 88, 1),		/* IPP: 2400 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 34),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 40),
	UNIPHIER_CLK_FACTOR("spi", -1, "spll", 1, 40),
	UNIPHIER_LD20_SYS_CLK_SD,
	UNIPHIER_LD11_SYS_CLK_NAND(2),
	UNIPHIER_SYS_CLK_NAND_4X(3),
	UNIPHIER_LD11_SYS_CLK_EMMC(4),
	UNIPHIER_CLK_GATE("ether0", 6, NULL, 0x210c, 9),
	UNIPHIER_CLK_GATE("ether1", 7, NULL, 0x210c, 10),
	UNIPHIER_CLK_GATE("usb30", 12, NULL, 0x210c, 4),	/* =GIO0 */
	UNIPHIER_CLK_GATE("usb31-0", 13, NULL, 0x210c, 5),	/* =GIO1 */
	UNIPHIER_CLK_GATE("usb31-1", 14, NULL, 0x210c, 6),	/* =GIO1-1 */
	UNIPHIER_CLK_GATE("usb30-hsphy0", 16, NULL, 0x210c, 16),
	UNIPHIER_CLK_GATE("usb30-ssphy0", 17, NULL, 0x210c, 18),
	UNIPHIER_CLK_GATE("usb30-ssphy1", 18, NULL, 0x210c, 20),
	UNIPHIER_CLK_GATE("usb31-hsphy0", 20, NULL, 0x210c, 17),
	UNIPHIER_CLK_GATE("usb31-ssphy0", 21, NULL, 0x210c, 19),
	UNIPHIER_CLK_GATE("pcie", 24, NULL, 0x210c, 3),
	UNIPHIER_CLK_GATE("sata0", 28, NULL, 0x210c, 7),
	UNIPHIER_CLK_GATE("sata1", 29, NULL, 0x210c, 8),
	UNIPHIER_CLK_GATE("sata-phy", 30, NULL, 0x210c, 21),
	UNIPHIER_LD11_SYS_CLK_AIO(40),
	UNIPHIER_LD11_SYS_CLK_EXIV(42),
	/* CPU gears */
	UNIPHIER_CLK_DIV4("cpll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV4("spll", 2, 3, 4, 8),
	UNIPHIER_CLK_DIV4("s2pll", 2, 3, 4, 8),
	UNIPHIER_CLK_CPUGEAR("cpu-ca53", 33, 0x8080, 0xf, 8,
			     "cpll/2", "spll/2", "cpll/3", "spll/3",
			     "spll/4", "spll/8", "cpll/4", "cpll/8"),
	UNIPHIER_CLK_CPUGEAR("cpu-ipp", 34, 0x8100, 0xf, 8,
			     "s2pll/2", "spll/2", "s2pll/3", "spll/3",
			     "spll/4", "spll/8", "s2pll/4", "s2pll/8"),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_nx1_sys_clk_data[] = {
	UNIPHIER_CLK_FACTOR("cpll", -1, "ref", 100, 1),		/* ARM: 2500 MHz */
	UNIPHIER_CLK_FACTOR("spll", -1, "ref", 32, 1),		/* 800 MHz */
	UNIPHIER_CLK_FACTOR("uart", 0, "spll", 1, 6),
	UNIPHIER_CLK_FACTOR("i2c", 1, "spll", 1, 16),
	UNIPHIER_NX1_SYS_CLK_SD,
	UNIPHIER_CLK_GATE("emmc", 4, NULL, 0x2108, 8),
	UNIPHIER_CLK_GATE("ether", 6, NULL, 0x210c, 0),
	UNIPHIER_CLK_GATE("usb30-0", 12, NULL, 0x210c, 16),	/* =GIO */
	UNIPHIER_CLK_GATE("usb30-1", 13, NULL, 0x210c, 20),	/* =GIO1P */
	UNIPHIER_CLK_GATE("usb30-hsphy0", 16, NULL, 0x210c, 24),
	UNIPHIER_CLK_GATE("usb30-ssphy0", 17, NULL, 0x210c, 25),
	UNIPHIER_CLK_GATE("usb30-ssphy1", 18, NULL, 0x210c, 26),
	UNIPHIER_CLK_GATE("pcie", 24, NULL, 0x210c, 8),
	UNIPHIER_CLK_GATE("voc", 52, NULL, 0x2110, 0),
	UNIPHIER_CLK_GATE("hdmitx", 58, NULL, 0x2110, 8),
	/* CPU gears */
	UNIPHIER_CLK_DIV5("cpll", 2, 4, 8, 16, 32),
	UNIPHIER_CLK_CPUGEAR("cpu-ca53", 33, 0x8080, 0xf, 5,
			     "cpll/2", "cpll/4", "cpll/8", "cpll/16",
			     "cpll/32"),
	{ /* sentinel */ }
};

const struct uniphier_clk_data uniphier_pro4_sg_clk_data[] = {
	UNIPHIER_CLK_DIV("gpll", 4),
	{
		.name = "sata-ref",
		.type = UNIPHIER_CLK_TYPE_MUX,
		.idx = 0,
		.data.mux = {
			.parent_names = { "gpll/4", "ref", },
			.num_parents = 2,
			.reg = 0x1a28,
			.masks = { 0x1, 0x1, },
			.vals  = { 0x0, 0x1, },
		},
	},
	{ /* sentinel */ }
};
