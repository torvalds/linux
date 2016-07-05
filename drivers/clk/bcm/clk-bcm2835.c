/*
 * Copyright (C) 2010,2015 Broadcom
 * Copyright (C) 2012 Stephen Warren
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

/**
 * DOC: BCM2835 CPRMAN (clock manager for the "audio" domain)
 *
 * The clock tree on the 2835 has several levels.  There's a root
 * oscillator running at 19.2Mhz.  After the oscillator there are 5
 * PLLs, roughly divided as "camera", "ARM", "core", "DSI displays",
 * and "HDMI displays".  Those 5 PLLs each can divide their output to
 * produce up to 4 channels.  Finally, there is the level of clocks to
 * be consumed by other hardware components (like "H264" or "HDMI
 * state machine"), which divide off of some subset of the PLL
 * channels.
 *
 * All of the clocks in the tree are exposed in the DT, because the DT
 * may want to make assignments of the final layer of clocks to the
 * PLL channels, and some components of the hardware will actually
 * skip layers of the tree (for example, the pixel clock comes
 * directly from the PLLH PIX channel without using a CM_*CTL clock
 * generator).
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clk/bcm2835.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <dt-bindings/clock/bcm2835.h>

#define CM_PASSWORD		0x5a000000

#define CM_GNRICCTL		0x000
#define CM_GNRICDIV		0x004
# define CM_DIV_FRAC_BITS	12

#define CM_VPUCTL		0x008
#define CM_VPUDIV		0x00c
#define CM_SYSCTL		0x010
#define CM_SYSDIV		0x014
#define CM_PERIACTL		0x018
#define CM_PERIADIV		0x01c
#define CM_PERIICTL		0x020
#define CM_PERIIDIV		0x024
#define CM_H264CTL		0x028
#define CM_H264DIV		0x02c
#define CM_ISPCTL		0x030
#define CM_ISPDIV		0x034
#define CM_V3DCTL		0x038
#define CM_V3DDIV		0x03c
#define CM_CAM0CTL		0x040
#define CM_CAM0DIV		0x044
#define CM_CAM1CTL		0x048
#define CM_CAM1DIV		0x04c
#define CM_CCP2CTL		0x050
#define CM_CCP2DIV		0x054
#define CM_DSI0ECTL		0x058
#define CM_DSI0EDIV		0x05c
#define CM_DSI0PCTL		0x060
#define CM_DSI0PDIV		0x064
#define CM_DPICTL		0x068
#define CM_DPIDIV		0x06c
#define CM_GP0CTL		0x070
#define CM_GP0DIV		0x074
#define CM_GP1CTL		0x078
#define CM_GP1DIV		0x07c
#define CM_GP2CTL		0x080
#define CM_GP2DIV		0x084
#define CM_HSMCTL		0x088
#define CM_HSMDIV		0x08c
#define CM_OTPCTL		0x090
#define CM_OTPDIV		0x094
#define CM_PWMCTL		0x0a0
#define CM_PWMDIV		0x0a4
#define CM_SMICTL		0x0b0
#define CM_SMIDIV		0x0b4
#define CM_TSENSCTL		0x0e0
#define CM_TSENSDIV		0x0e4
#define CM_TIMERCTL		0x0e8
#define CM_TIMERDIV		0x0ec
#define CM_UARTCTL		0x0f0
#define CM_UARTDIV		0x0f4
#define CM_VECCTL		0x0f8
#define CM_VECDIV		0x0fc
#define CM_PULSECTL		0x190
#define CM_PULSEDIV		0x194
#define CM_SDCCTL		0x1a8
#define CM_SDCDIV		0x1ac
#define CM_ARMCTL		0x1b0
#define CM_EMMCCTL		0x1c0
#define CM_EMMCDIV		0x1c4

/* General bits for the CM_*CTL regs */
# define CM_ENABLE			BIT(4)
# define CM_KILL			BIT(5)
# define CM_GATE_BIT			6
# define CM_GATE			BIT(CM_GATE_BIT)
# define CM_BUSY			BIT(7)
# define CM_BUSYD			BIT(8)
# define CM_SRC_SHIFT			0
# define CM_SRC_BITS			4
# define CM_SRC_MASK			0xf
# define CM_SRC_GND			0
# define CM_SRC_OSC			1
# define CM_SRC_TESTDEBUG0		2
# define CM_SRC_TESTDEBUG1		3
# define CM_SRC_PLLA_CORE		4
# define CM_SRC_PLLA_PER		4
# define CM_SRC_PLLC_CORE0		5
# define CM_SRC_PLLC_PER		5
# define CM_SRC_PLLC_CORE1		8
# define CM_SRC_PLLD_CORE		6
# define CM_SRC_PLLD_PER		6
# define CM_SRC_PLLH_AUX		7
# define CM_SRC_PLLC_CORE1		8
# define CM_SRC_PLLC_CORE2		9

#define CM_OSCCOUNT		0x100

#define CM_PLLA			0x104
# define CM_PLL_ANARST			BIT(8)
# define CM_PLLA_HOLDPER		BIT(7)
# define CM_PLLA_LOADPER		BIT(6)
# define CM_PLLA_HOLDCORE		BIT(5)
# define CM_PLLA_LOADCORE		BIT(4)
# define CM_PLLA_HOLDCCP2		BIT(3)
# define CM_PLLA_LOADCCP2		BIT(2)
# define CM_PLLA_HOLDDSI0		BIT(1)
# define CM_PLLA_LOADDSI0		BIT(0)

#define CM_PLLC			0x108
# define CM_PLLC_HOLDPER		BIT(7)
# define CM_PLLC_LOADPER		BIT(6)
# define CM_PLLC_HOLDCORE2		BIT(5)
# define CM_PLLC_LOADCORE2		BIT(4)
# define CM_PLLC_HOLDCORE1		BIT(3)
# define CM_PLLC_LOADCORE1		BIT(2)
# define CM_PLLC_HOLDCORE0		BIT(1)
# define CM_PLLC_LOADCORE0		BIT(0)

#define CM_PLLD			0x10c
# define CM_PLLD_HOLDPER		BIT(7)
# define CM_PLLD_LOADPER		BIT(6)
# define CM_PLLD_HOLDCORE		BIT(5)
# define CM_PLLD_LOADCORE		BIT(4)
# define CM_PLLD_HOLDDSI1		BIT(3)
# define CM_PLLD_LOADDSI1		BIT(2)
# define CM_PLLD_HOLDDSI0		BIT(1)
# define CM_PLLD_LOADDSI0		BIT(0)

#define CM_PLLH			0x110
# define CM_PLLH_LOADRCAL		BIT(2)
# define CM_PLLH_LOADAUX		BIT(1)
# define CM_PLLH_LOADPIX		BIT(0)

#define CM_LOCK			0x114
# define CM_LOCK_FLOCKH			BIT(12)
# define CM_LOCK_FLOCKD			BIT(11)
# define CM_LOCK_FLOCKC			BIT(10)
# define CM_LOCK_FLOCKB			BIT(9)
# define CM_LOCK_FLOCKA			BIT(8)

#define CM_EVENT		0x118
#define CM_DSI1ECTL		0x158
#define CM_DSI1EDIV		0x15c
#define CM_DSI1PCTL		0x160
#define CM_DSI1PDIV		0x164
#define CM_DFTCTL		0x168
#define CM_DFTDIV		0x16c

#define CM_PLLB			0x170
# define CM_PLLB_HOLDARM		BIT(1)
# define CM_PLLB_LOADARM		BIT(0)

#define A2W_PLLA_CTRL		0x1100
#define A2W_PLLC_CTRL		0x1120
#define A2W_PLLD_CTRL		0x1140
#define A2W_PLLH_CTRL		0x1160
#define A2W_PLLB_CTRL		0x11e0
# define A2W_PLL_CTRL_PRST_DISABLE	BIT(17)
# define A2W_PLL_CTRL_PWRDN		BIT(16)
# define A2W_PLL_CTRL_PDIV_MASK		0x000007000
# define A2W_PLL_CTRL_PDIV_SHIFT	12
# define A2W_PLL_CTRL_NDIV_MASK		0x0000003ff
# define A2W_PLL_CTRL_NDIV_SHIFT	0

#define A2W_PLLA_ANA0		0x1010
#define A2W_PLLC_ANA0		0x1030
#define A2W_PLLD_ANA0		0x1050
#define A2W_PLLH_ANA0		0x1070
#define A2W_PLLB_ANA0		0x10f0

#define A2W_PLL_KA_SHIFT	7
#define A2W_PLL_KA_MASK		GENMASK(9, 7)
#define A2W_PLL_KI_SHIFT	19
#define A2W_PLL_KI_MASK		GENMASK(21, 19)
#define A2W_PLL_KP_SHIFT	15
#define A2W_PLL_KP_MASK		GENMASK(18, 15)

#define A2W_PLLH_KA_SHIFT	19
#define A2W_PLLH_KA_MASK	GENMASK(21, 19)
#define A2W_PLLH_KI_LOW_SHIFT	22
#define A2W_PLLH_KI_LOW_MASK	GENMASK(23, 22)
#define A2W_PLLH_KI_HIGH_SHIFT	0
#define A2W_PLLH_KI_HIGH_MASK	GENMASK(0, 0)
#define A2W_PLLH_KP_SHIFT	1
#define A2W_PLLH_KP_MASK	GENMASK(4, 1)

#define A2W_XOSC_CTRL		0x1190
# define A2W_XOSC_CTRL_PLLB_ENABLE	BIT(7)
# define A2W_XOSC_CTRL_PLLA_ENABLE	BIT(6)
# define A2W_XOSC_CTRL_PLLD_ENABLE	BIT(5)
# define A2W_XOSC_CTRL_DDR_ENABLE	BIT(4)
# define A2W_XOSC_CTRL_CPR1_ENABLE	BIT(3)
# define A2W_XOSC_CTRL_USB_ENABLE	BIT(2)
# define A2W_XOSC_CTRL_HDMI_ENABLE	BIT(1)
# define A2W_XOSC_CTRL_PLLC_ENABLE	BIT(0)

#define A2W_PLLA_FRAC		0x1200
#define A2W_PLLC_FRAC		0x1220
#define A2W_PLLD_FRAC		0x1240
#define A2W_PLLH_FRAC		0x1260
#define A2W_PLLB_FRAC		0x12e0
# define A2W_PLL_FRAC_MASK		((1 << A2W_PLL_FRAC_BITS) - 1)
# define A2W_PLL_FRAC_BITS		20

#define A2W_PLL_CHANNEL_DISABLE		BIT(8)
#define A2W_PLL_DIV_BITS		8
#define A2W_PLL_DIV_SHIFT		0

#define A2W_PLLA_DSI0		0x1300
#define A2W_PLLA_CORE		0x1400
#define A2W_PLLA_PER		0x1500
#define A2W_PLLA_CCP2		0x1600

#define A2W_PLLC_CORE2		0x1320
#define A2W_PLLC_CORE1		0x1420
#define A2W_PLLC_PER		0x1520
#define A2W_PLLC_CORE0		0x1620

#define A2W_PLLD_DSI0		0x1340
#define A2W_PLLD_CORE		0x1440
#define A2W_PLLD_PER		0x1540
#define A2W_PLLD_DSI1		0x1640

#define A2W_PLLH_AUX		0x1360
#define A2W_PLLH_RCAL		0x1460
#define A2W_PLLH_PIX		0x1560
#define A2W_PLLH_STS		0x1660

#define A2W_PLLH_CTRLR		0x1960
#define A2W_PLLH_FRACR		0x1a60
#define A2W_PLLH_AUXR		0x1b60
#define A2W_PLLH_RCALR		0x1c60
#define A2W_PLLH_PIXR		0x1d60
#define A2W_PLLH_STSR		0x1e60

#define A2W_PLLB_ARM		0x13e0
#define A2W_PLLB_SP0		0x14e0
#define A2W_PLLB_SP1		0x15e0
#define A2W_PLLB_SP2		0x16e0

#define LOCK_TIMEOUT_NS		100000000
#define BCM2835_MAX_FB_RATE	1750000000u

struct bcm2835_cprman {
	struct device *dev;
	void __iomem *regs;
	spinlock_t regs_lock;
	const char *osc_name;

	struct clk_onecell_data onecell;
	struct clk *clks[BCM2835_CLOCK_COUNT];
};

static inline void cprman_write(struct bcm2835_cprman *cprman, u32 reg, u32 val)
{
	writel(CM_PASSWORD | val, cprman->regs + reg);
}

static inline u32 cprman_read(struct bcm2835_cprman *cprman, u32 reg)
{
	return readl(cprman->regs + reg);
}

/*
 * These are fixed clocks. They're probably not all root clocks and it may
 * be possible to turn them on and off but until this is mapped out better
 * it's the only way they can be used.
 */
void __init bcm2835_init_clocks(void)
{
	struct clk *clk;
	int ret;

	clk = clk_register_fixed_rate(NULL, "apb_pclk", NULL, CLK_IS_ROOT,
					126000000);
	if (IS_ERR(clk))
		pr_err("apb_pclk not registered\n");

	clk = clk_register_fixed_rate(NULL, "uart0_pclk", NULL, CLK_IS_ROOT,
					3000000);
	if (IS_ERR(clk))
		pr_err("uart0_pclk not registered\n");
	ret = clk_register_clkdev(clk, NULL, "20201000.uart");
	if (ret)
		pr_err("uart0_pclk alias not registered\n");

	clk = clk_register_fixed_rate(NULL, "uart1_pclk", NULL, CLK_IS_ROOT,
					125000000);
	if (IS_ERR(clk))
		pr_err("uart1_pclk not registered\n");
	ret = clk_register_clkdev(clk, NULL, "20215000.uart");
	if (ret)
		pr_err("uart1_pclk alias not registered\n");
}

struct bcm2835_pll_data {
	const char *name;
	u32 cm_ctrl_reg;
	u32 a2w_ctrl_reg;
	u32 frac_reg;
	u32 ana_reg_base;
	u32 reference_enable_mask;
	/* Bit in CM_LOCK to indicate when the PLL has locked. */
	u32 lock_mask;

	const struct bcm2835_pll_ana_bits *ana;

	unsigned long min_rate;
	unsigned long max_rate;
	/*
	 * Highest rate for the VCO before we have to use the
	 * pre-divide-by-2.
	 */
	unsigned long max_fb_rate;
};

struct bcm2835_pll_ana_bits {
	u32 mask0;
	u32 set0;
	u32 mask1;
	u32 set1;
	u32 mask3;
	u32 set3;
	u32 fb_prediv_mask;
};

static const struct bcm2835_pll_ana_bits bcm2835_ana_default = {
	.mask0 = 0,
	.set0 = 0,
	.mask1 = ~(A2W_PLL_KI_MASK | A2W_PLL_KP_MASK),
	.set1 = (2 << A2W_PLL_KI_SHIFT) | (8 << A2W_PLL_KP_SHIFT),
	.mask3 = ~A2W_PLL_KA_MASK,
	.set3 = (2 << A2W_PLL_KA_SHIFT),
	.fb_prediv_mask = BIT(14),
};

static const struct bcm2835_pll_ana_bits bcm2835_ana_pllh = {
	.mask0 = ~(A2W_PLLH_KA_MASK | A2W_PLLH_KI_LOW_MASK),
	.set0 = (2 << A2W_PLLH_KA_SHIFT) | (2 << A2W_PLLH_KI_LOW_SHIFT),
	.mask1 = ~(A2W_PLLH_KI_HIGH_MASK | A2W_PLLH_KP_MASK),
	.set1 = (6 << A2W_PLLH_KP_SHIFT),
	.mask3 = 0,
	.set3 = 0,
	.fb_prediv_mask = BIT(11),
};

/*
 * PLLA is the auxiliary PLL, used to drive the CCP2 (Compact Camera
 * Port 2) transmitter clock.
 *
 * It is in the PX LDO power domain, which is on when the AUDIO domain
 * is on.
 */
static const struct bcm2835_pll_data bcm2835_plla_data = {
	.name = "plla",
	.cm_ctrl_reg = CM_PLLA,
	.a2w_ctrl_reg = A2W_PLLA_CTRL,
	.frac_reg = A2W_PLLA_FRAC,
	.ana_reg_base = A2W_PLLA_ANA0,
	.reference_enable_mask = A2W_XOSC_CTRL_PLLA_ENABLE,
	.lock_mask = CM_LOCK_FLOCKA,

	.ana = &bcm2835_ana_default,

	.min_rate = 600000000u,
	.max_rate = 2400000000u,
	.max_fb_rate = BCM2835_MAX_FB_RATE,
};

/* PLLB is used for the ARM's clock. */
static const struct bcm2835_pll_data bcm2835_pllb_data = {
	.name = "pllb",
	.cm_ctrl_reg = CM_PLLB,
	.a2w_ctrl_reg = A2W_PLLB_CTRL,
	.frac_reg = A2W_PLLB_FRAC,
	.ana_reg_base = A2W_PLLB_ANA0,
	.reference_enable_mask = A2W_XOSC_CTRL_PLLB_ENABLE,
	.lock_mask = CM_LOCK_FLOCKB,

	.ana = &bcm2835_ana_default,

	.min_rate = 600000000u,
	.max_rate = 3000000000u,
	.max_fb_rate = BCM2835_MAX_FB_RATE,
};

/*
 * PLLC is the core PLL, used to drive the core VPU clock.
 *
 * It is in the PX LDO power domain, which is on when the AUDIO domain
 * is on.
*/
static const struct bcm2835_pll_data bcm2835_pllc_data = {
	.name = "pllc",
	.cm_ctrl_reg = CM_PLLC,
	.a2w_ctrl_reg = A2W_PLLC_CTRL,
	.frac_reg = A2W_PLLC_FRAC,
	.ana_reg_base = A2W_PLLC_ANA0,
	.reference_enable_mask = A2W_XOSC_CTRL_PLLC_ENABLE,
	.lock_mask = CM_LOCK_FLOCKC,

	.ana = &bcm2835_ana_default,

	.min_rate = 600000000u,
	.max_rate = 3000000000u,
	.max_fb_rate = BCM2835_MAX_FB_RATE,
};

/*
 * PLLD is the display PLL, used to drive DSI display panels.
 *
 * It is in the PX LDO power domain, which is on when the AUDIO domain
 * is on.
 */
static const struct bcm2835_pll_data bcm2835_plld_data = {
	.name = "plld",
	.cm_ctrl_reg = CM_PLLD,
	.a2w_ctrl_reg = A2W_PLLD_CTRL,
	.frac_reg = A2W_PLLD_FRAC,
	.ana_reg_base = A2W_PLLD_ANA0,
	.reference_enable_mask = A2W_XOSC_CTRL_DDR_ENABLE,
	.lock_mask = CM_LOCK_FLOCKD,

	.ana = &bcm2835_ana_default,

	.min_rate = 600000000u,
	.max_rate = 2400000000u,
	.max_fb_rate = BCM2835_MAX_FB_RATE,
};

/*
 * PLLH is used to supply the pixel clock or the AUX clock for the TV
 * encoder.
 *
 * It is in the HDMI power domain.
 */
static const struct bcm2835_pll_data bcm2835_pllh_data = {
	"pllh",
	.cm_ctrl_reg = CM_PLLH,
	.a2w_ctrl_reg = A2W_PLLH_CTRL,
	.frac_reg = A2W_PLLH_FRAC,
	.ana_reg_base = A2W_PLLH_ANA0,
	.reference_enable_mask = A2W_XOSC_CTRL_PLLC_ENABLE,
	.lock_mask = CM_LOCK_FLOCKH,

	.ana = &bcm2835_ana_pllh,

	.min_rate = 600000000u,
	.max_rate = 3000000000u,
	.max_fb_rate = BCM2835_MAX_FB_RATE,
};

struct bcm2835_pll_divider_data {
	const char *name;
	const struct bcm2835_pll_data *source_pll;
	u32 cm_reg;
	u32 a2w_reg;

	u32 load_mask;
	u32 hold_mask;
	u32 fixed_divider;
};

static const struct bcm2835_pll_divider_data bcm2835_plla_core_data = {
	.name = "plla_core",
	.source_pll = &bcm2835_plla_data,
	.cm_reg = CM_PLLA,
	.a2w_reg = A2W_PLLA_CORE,
	.load_mask = CM_PLLA_LOADCORE,
	.hold_mask = CM_PLLA_HOLDCORE,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_plla_per_data = {
	.name = "plla_per",
	.source_pll = &bcm2835_plla_data,
	.cm_reg = CM_PLLA,
	.a2w_reg = A2W_PLLA_PER,
	.load_mask = CM_PLLA_LOADPER,
	.hold_mask = CM_PLLA_HOLDPER,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllb_arm_data = {
	.name = "pllb_arm",
	.source_pll = &bcm2835_pllb_data,
	.cm_reg = CM_PLLB,
	.a2w_reg = A2W_PLLB_ARM,
	.load_mask = CM_PLLB_LOADARM,
	.hold_mask = CM_PLLB_HOLDARM,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllc_core0_data = {
	.name = "pllc_core0",
	.source_pll = &bcm2835_pllc_data,
	.cm_reg = CM_PLLC,
	.a2w_reg = A2W_PLLC_CORE0,
	.load_mask = CM_PLLC_LOADCORE0,
	.hold_mask = CM_PLLC_HOLDCORE0,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllc_core1_data = {
	.name = "pllc_core1", .source_pll = &bcm2835_pllc_data,
	.cm_reg = CM_PLLC, A2W_PLLC_CORE1,
	.load_mask = CM_PLLC_LOADCORE1,
	.hold_mask = CM_PLLC_HOLDCORE1,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllc_core2_data = {
	.name = "pllc_core2",
	.source_pll = &bcm2835_pllc_data,
	.cm_reg = CM_PLLC,
	.a2w_reg = A2W_PLLC_CORE2,
	.load_mask = CM_PLLC_LOADCORE2,
	.hold_mask = CM_PLLC_HOLDCORE2,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllc_per_data = {
	.name = "pllc_per",
	.source_pll = &bcm2835_pllc_data,
	.cm_reg = CM_PLLC,
	.a2w_reg = A2W_PLLC_PER,
	.load_mask = CM_PLLC_LOADPER,
	.hold_mask = CM_PLLC_HOLDPER,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_plld_core_data = {
	.name = "plld_core",
	.source_pll = &bcm2835_plld_data,
	.cm_reg = CM_PLLD,
	.a2w_reg = A2W_PLLD_CORE,
	.load_mask = CM_PLLD_LOADCORE,
	.hold_mask = CM_PLLD_HOLDCORE,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_plld_per_data = {
	.name = "plld_per",
	.source_pll = &bcm2835_plld_data,
	.cm_reg = CM_PLLD,
	.a2w_reg = A2W_PLLD_PER,
	.load_mask = CM_PLLD_LOADPER,
	.hold_mask = CM_PLLD_HOLDPER,
	.fixed_divider = 1,
};

static const struct bcm2835_pll_divider_data bcm2835_pllh_rcal_data = {
	.name = "pllh_rcal",
	.source_pll = &bcm2835_pllh_data,
	.cm_reg = CM_PLLH,
	.a2w_reg = A2W_PLLH_RCAL,
	.load_mask = CM_PLLH_LOADRCAL,
	.hold_mask = 0,
	.fixed_divider = 10,
};

static const struct bcm2835_pll_divider_data bcm2835_pllh_aux_data = {
	.name = "pllh_aux",
	.source_pll = &bcm2835_pllh_data,
	.cm_reg = CM_PLLH,
	.a2w_reg = A2W_PLLH_AUX,
	.load_mask = CM_PLLH_LOADAUX,
	.hold_mask = 0,
	.fixed_divider = 10,
};

static const struct bcm2835_pll_divider_data bcm2835_pllh_pix_data = {
	.name = "pllh_pix",
	.source_pll = &bcm2835_pllh_data,
	.cm_reg = CM_PLLH,
	.a2w_reg = A2W_PLLH_PIX,
	.load_mask = CM_PLLH_LOADPIX,
	.hold_mask = 0,
	.fixed_divider = 10,
};

struct bcm2835_clock_data {
	const char *name;

	const char *const *parents;
	int num_mux_parents;

	u32 ctl_reg;
	u32 div_reg;

	/* Number of integer bits in the divider */
	u32 int_bits;
	/* Number of fractional bits in the divider */
	u32 frac_bits;

	bool is_vpu_clock;
};

static const char *const bcm2835_clock_per_parents[] = {
	"gnd",
	"xosc",
	"testdebug0",
	"testdebug1",
	"plla_per",
	"pllc_per",
	"plld_per",
	"pllh_aux",
};

static const char *const bcm2835_clock_vpu_parents[] = {
	"gnd",
	"xosc",
	"testdebug0",
	"testdebug1",
	"plla_core",
	"pllc_core0",
	"plld_core",
	"pllh_aux",
	"pllc_core1",
	"pllc_core2",
};

static const char *const bcm2835_clock_osc_parents[] = {
	"gnd",
	"xosc",
	"testdebug0",
	"testdebug1"
};

/*
 * Used for a 1Mhz clock for the system clocksource, and also used by
 * the watchdog timer and the camera pulse generator.
 */
static const struct bcm2835_clock_data bcm2835_clock_timer_data = {
	.name = "timer",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_osc_parents),
	.parents = bcm2835_clock_osc_parents,
	.ctl_reg = CM_TIMERCTL,
	.div_reg = CM_TIMERDIV,
	.int_bits = 6,
	.frac_bits = 12,
};

/* One Time Programmable Memory clock.  Maximum 10Mhz. */
static const struct bcm2835_clock_data bcm2835_clock_otp_data = {
	.name = "otp",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_osc_parents),
	.parents = bcm2835_clock_osc_parents,
	.ctl_reg = CM_OTPCTL,
	.div_reg = CM_OTPDIV,
	.int_bits = 4,
	.frac_bits = 0,
};

/*
 * VPU clock.  This doesn't have an enable bit, since it drives the
 * bus for everything else, and is special so it doesn't need to be
 * gated for rate changes.  It is also known as "clk_audio" in various
 * hardware documentation.
 */
static const struct bcm2835_clock_data bcm2835_clock_vpu_data = {
	.name = "vpu",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_vpu_parents),
	.parents = bcm2835_clock_vpu_parents,
	.ctl_reg = CM_VPUCTL,
	.div_reg = CM_VPUDIV,
	.int_bits = 12,
	.frac_bits = 8,
	.is_vpu_clock = true,
};

static const struct bcm2835_clock_data bcm2835_clock_v3d_data = {
	.name = "v3d",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_vpu_parents),
	.parents = bcm2835_clock_vpu_parents,
	.ctl_reg = CM_V3DCTL,
	.div_reg = CM_V3DDIV,
	.int_bits = 4,
	.frac_bits = 8,
};

static const struct bcm2835_clock_data bcm2835_clock_isp_data = {
	.name = "isp",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_vpu_parents),
	.parents = bcm2835_clock_vpu_parents,
	.ctl_reg = CM_ISPCTL,
	.div_reg = CM_ISPDIV,
	.int_bits = 4,
	.frac_bits = 8,
};

static const struct bcm2835_clock_data bcm2835_clock_h264_data = {
	.name = "h264",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_vpu_parents),
	.parents = bcm2835_clock_vpu_parents,
	.ctl_reg = CM_H264CTL,
	.div_reg = CM_H264DIV,
	.int_bits = 4,
	.frac_bits = 8,
};

/* TV encoder clock.  Only operating frequency is 108Mhz.  */
static const struct bcm2835_clock_data bcm2835_clock_vec_data = {
	.name = "vec",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_per_parents),
	.parents = bcm2835_clock_per_parents,
	.ctl_reg = CM_VECCTL,
	.div_reg = CM_VECDIV,
	.int_bits = 4,
	.frac_bits = 0,
};

static const struct bcm2835_clock_data bcm2835_clock_uart_data = {
	.name = "uart",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_per_parents),
	.parents = bcm2835_clock_per_parents,
	.ctl_reg = CM_UARTCTL,
	.div_reg = CM_UARTDIV,
	.int_bits = 10,
	.frac_bits = 12,
};

/* HDMI state machine */
static const struct bcm2835_clock_data bcm2835_clock_hsm_data = {
	.name = "hsm",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_per_parents),
	.parents = bcm2835_clock_per_parents,
	.ctl_reg = CM_HSMCTL,
	.div_reg = CM_HSMDIV,
	.int_bits = 4,
	.frac_bits = 8,
};

/*
 * Secondary SDRAM clock.  Used for low-voltage modes when the PLL in
 * the SDRAM controller can't be used.
 */
static const struct bcm2835_clock_data bcm2835_clock_sdram_data = {
	.name = "sdram",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_vpu_parents),
	.parents = bcm2835_clock_vpu_parents,
	.ctl_reg = CM_SDCCTL,
	.div_reg = CM_SDCDIV,
	.int_bits = 6,
	.frac_bits = 0,
};

/* Clock for the temperature sensor.  Generally run at 2Mhz, max 5Mhz. */
static const struct bcm2835_clock_data bcm2835_clock_tsens_data = {
	.name = "tsens",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_osc_parents),
	.parents = bcm2835_clock_osc_parents,
	.ctl_reg = CM_TSENSCTL,
	.div_reg = CM_TSENSDIV,
	.int_bits = 5,
	.frac_bits = 0,
};

/* Arasan EMMC clock */
static const struct bcm2835_clock_data bcm2835_clock_emmc_data = {
	.name = "emmc",
	.num_mux_parents = ARRAY_SIZE(bcm2835_clock_per_parents),
	.parents = bcm2835_clock_per_parents,
	.ctl_reg = CM_EMMCCTL,
	.div_reg = CM_EMMCDIV,
	.int_bits = 4,
	.frac_bits = 8,
};

struct bcm2835_pll {
	struct clk_hw hw;
	struct bcm2835_cprman *cprman;
	const struct bcm2835_pll_data *data;
};

static int bcm2835_pll_is_on(struct clk_hw *hw)
{
	struct bcm2835_pll *pll = container_of(hw, struct bcm2835_pll, hw);
	struct bcm2835_cprman *cprman = pll->cprman;
	const struct bcm2835_pll_data *data = pll->data;

	return cprman_read(cprman, data->a2w_ctrl_reg) &
		A2W_PLL_CTRL_PRST_DISABLE;
}

static void bcm2835_pll_choose_ndiv_and_fdiv(unsigned long rate,
					     unsigned long parent_rate,
					     u32 *ndiv, u32 *fdiv)
{
	u64 div;

	div = (u64)rate << A2W_PLL_FRAC_BITS;
	do_div(div, parent_rate);

	*ndiv = div >> A2W_PLL_FRAC_BITS;
	*fdiv = div & ((1 << A2W_PLL_FRAC_BITS) - 1);
}

static long bcm2835_pll_rate_from_divisors(unsigned long parent_rate,
					   u32 ndiv, u32 fdiv, u32 pdiv)
{
	u64 rate;

	if (pdiv == 0)
		return 0;

	rate = (u64)parent_rate * ((ndiv << A2W_PLL_FRAC_BITS) + fdiv);
	do_div(rate, pdiv);
	return rate >> A2W_PLL_FRAC_BITS;
}

static long bcm2835_pll_round_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long *parent_rate)
{
	u32 ndiv, fdiv;

	bcm2835_pll_choose_ndiv_and_fdiv(rate, *parent_rate, &ndiv, &fdiv);

	return bcm2835_pll_rate_from_divisors(*parent_rate, ndiv, fdiv, 1);
}

static unsigned long bcm2835_pll_get_rate(struct clk_hw *hw,
					  unsigned long parent_rate)
{
	struct bcm2835_pll *pll = container_of(hw, struct bcm2835_pll, hw);
	struct bcm2835_cprman *cprman = pll->cprman;
	const struct bcm2835_pll_data *data = pll->data;
	u32 a2wctrl = cprman_read(cprman, data->a2w_ctrl_reg);
	u32 ndiv, pdiv, fdiv;
	bool using_prediv;

	if (parent_rate == 0)
		return 0;

	fdiv = cprman_read(cprman, data->frac_reg) & A2W_PLL_FRAC_MASK;
	ndiv = (a2wctrl & A2W_PLL_CTRL_NDIV_MASK) >> A2W_PLL_CTRL_NDIV_SHIFT;
	pdiv = (a2wctrl & A2W_PLL_CTRL_PDIV_MASK) >> A2W_PLL_CTRL_PDIV_SHIFT;
	using_prediv = cprman_read(cprman, data->ana_reg_base + 4) &
		data->ana->fb_prediv_mask;

	if (using_prediv)
		ndiv *= 2;

	return bcm2835_pll_rate_from_divisors(parent_rate, ndiv, fdiv, pdiv);
}

static void bcm2835_pll_off(struct clk_hw *hw)
{
	struct bcm2835_pll *pll = container_of(hw, struct bcm2835_pll, hw);
	struct bcm2835_cprman *cprman = pll->cprman;
	const struct bcm2835_pll_data *data = pll->data;

	spin_lock(&cprman->regs_lock);
	cprman_write(cprman, data->cm_ctrl_reg,
		     cprman_read(cprman, data->cm_ctrl_reg) |
		     CM_PLL_ANARST);
	cprman_write(cprman, data->a2w_ctrl_reg,
		     cprman_read(cprman, data->a2w_ctrl_reg) |
		     A2W_PLL_CTRL_PWRDN);
	spin_unlock(&cprman->regs_lock);
}

static int bcm2835_pll_on(struct clk_hw *hw)
{
	struct bcm2835_pll *pll = container_of(hw, struct bcm2835_pll, hw);
	struct bcm2835_cprman *cprman = pll->cprman;
	const struct bcm2835_pll_data *data = pll->data;
	ktime_t timeout;

	cprman_write(cprman, data->a2w_ctrl_reg,
		     cprman_read(cprman, data->a2w_ctrl_reg) &
		     ~A2W_PLL_CTRL_PWRDN);

	/* Take the PLL out of reset. */
	cprman_write(cprman, data->cm_ctrl_reg,
		     cprman_read(cprman, data->cm_ctrl_reg) & ~CM_PLL_ANARST);

	/* Wait for the PLL to lock. */
	timeout = ktime_add_ns(ktime_get(), LOCK_TIMEOUT_NS);
	while (!(cprman_read(cprman, CM_LOCK) & data->lock_mask)) {
		if (ktime_after(ktime_get(), timeout)) {
			dev_err(cprman->dev, "%s: couldn't lock PLL\n",
				clk_hw_get_name(hw));
			return -ETIMEDOUT;
		}

		cpu_relax();
	}

	return 0;
}

static void
bcm2835_pll_write_ana(struct bcm2835_cprman *cprman, u32 ana_reg_base, u32 *ana)
{
	int i;

	/*
	 * ANA register setup is done as a series of writes to
	 * ANA3-ANA0, in that order.  This lets us write all 4
	 * registers as a single cycle of the serdes interface (taking
	 * 100 xosc clocks), whereas if we were to update ana0, 1, and
	 * 3 individually through their partial-write registers, each
	 * would be their own serdes cycle.
	 */
	for (i = 3; i >= 0; i--)
		cprman_write(cprman, ana_reg_base + i * 4, ana[i]);
}

static int bcm2835_pll_set_rate(struct clk_hw *hw,
				unsigned long rate, unsigned long parent_rate)
{
	struct bcm2835_pll *pll = container_of(hw, struct bcm2835_pll, hw);
	struct bcm2835_cprman *cprman = pll->cprman;
	const struct bcm2835_pll_data *data = pll->data;
	bool was_using_prediv, use_fb_prediv, do_ana_setup_first;
	u32 ndiv, fdiv, a2w_ctl;
	u32 ana[4];
	int i;

	if (rate < data->min_rate || rate > data->max_rate) {
		dev_err(cprman->dev, "%s: rate out of spec: %lu vs (%lu, %lu)\n",
			clk_hw_get_name(hw), rate,
			data->min_rate, data->max_rate);
		return -EINVAL;
	}

	if (rate > data->max_fb_rate) {
		use_fb_prediv = true;
		rate /= 2;
	} else {
		use_fb_prediv = false;
	}

	bcm2835_pll_choose_ndiv_and_fdiv(rate, parent_rate, &ndiv, &fdiv);

	for (i = 3; i >= 0; i--)
		ana[i] = cprman_read(cprman, data->ana_reg_base + i * 4);

	was_using_prediv = ana[1] & data->ana->fb_prediv_mask;

	ana[0] &= ~data->ana->mask0;
	ana[0] |= data->ana->set0;
	ana[1] &= ~data->ana->mask1;
	ana[1] |= data->ana->set1;
	ana[3] &= ~data->ana->mask3;
	ana[3] |= data->ana->set3;

	if (was_using_prediv && !use_fb_prediv) {
		ana[1] &= ~data->ana->fb_prediv_mask;
		do_ana_setup_first = true;
	} else if (!was_using_prediv && use_fb_prediv) {
		ana[1] |= data->ana->fb_prediv_mask;
		do_ana_setup_first = false;
	} else {
		do_ana_setup_first = true;
	}

	/* Unmask the reference clock from the oscillator. */
	cprman_write(cprman, A2W_XOSC_CTRL,
		     cprman_read(cprman, A2W_XOSC_CTRL) |
		     data->reference_enable_mask);

	if (do_ana_setup_first)
		bcm2835_pll_write_ana(cprman, data->ana_reg_base, ana);

	/* Set the PLL multiplier from the oscillator. */
	cprman_write(cprman, data->frac_reg, fdiv);

	a2w_ctl = cprman_read(cprman, data->a2w_ctrl_reg);
	a2w_ctl &= ~A2W_PLL_CTRL_NDIV_MASK;
	a2w_ctl |= ndiv << A2W_PLL_CTRL_NDIV_SHIFT;
	a2w_ctl &= ~A2W_PLL_CTRL_PDIV_MASK;
	a2w_ctl |= 1 << A2W_PLL_CTRL_PDIV_SHIFT;
	cprman_write(cprman, data->a2w_ctrl_reg, a2w_ctl);

	if (!do_ana_setup_first)
		bcm2835_pll_write_ana(cprman, data->ana_reg_base, ana);

	return 0;
}

static const struct clk_ops bcm2835_pll_clk_ops = {
	.is_prepared = bcm2835_pll_is_on,
	.prepare = bcm2835_pll_on,
	.unprepare = bcm2835_pll_off,
	.recalc_rate = bcm2835_pll_get_rate,
	.set_rate = bcm2835_pll_set_rate,
	.round_rate = bcm2835_pll_round_rate,
};

struct bcm2835_pll_divider {
	struct clk_divider div;
	struct bcm2835_cprman *cprman;
	const struct bcm2835_pll_divider_data *data;
};

static struct bcm2835_pll_divider *
bcm2835_pll_divider_from_hw(struct clk_hw *hw)
{
	return container_of(hw, struct bcm2835_pll_divider, div.hw);
}

static int bcm2835_pll_divider_is_on(struct clk_hw *hw)
{
	struct bcm2835_pll_divider *divider = bcm2835_pll_divider_from_hw(hw);
	struct bcm2835_cprman *cprman = divider->cprman;
	const struct bcm2835_pll_divider_data *data = divider->data;

	return !(cprman_read(cprman, data->a2w_reg) & A2W_PLL_CHANNEL_DISABLE);
}

static long bcm2835_pll_divider_round_rate(struct clk_hw *hw,
					   unsigned long rate,
					   unsigned long *parent_rate)
{
	return clk_divider_ops.round_rate(hw, rate, parent_rate);
}

static unsigned long bcm2835_pll_divider_get_rate(struct clk_hw *hw,
						  unsigned long parent_rate)
{
	struct bcm2835_pll_divider *divider = bcm2835_pll_divider_from_hw(hw);
	struct bcm2835_cprman *cprman = divider->cprman;
	const struct bcm2835_pll_divider_data *data = divider->data;
	u32 div = cprman_read(cprman, data->a2w_reg);

	div &= (1 << A2W_PLL_DIV_BITS) - 1;
	if (div == 0)
		div = 256;

	return parent_rate / div;
}

static void bcm2835_pll_divider_off(struct clk_hw *hw)
{
	struct bcm2835_pll_divider *divider = bcm2835_pll_divider_from_hw(hw);
	struct bcm2835_cprman *cprman = divider->cprman;
	const struct bcm2835_pll_divider_data *data = divider->data;

	spin_lock(&cprman->regs_lock);
	cprman_write(cprman, data->cm_reg,
		     (cprman_read(cprman, data->cm_reg) &
		      ~data->load_mask) | data->hold_mask);
	cprman_write(cprman, data->a2w_reg, A2W_PLL_CHANNEL_DISABLE);
	spin_unlock(&cprman->regs_lock);
}

static int bcm2835_pll_divider_on(struct clk_hw *hw)
{
	struct bcm2835_pll_divider *divider = bcm2835_pll_divider_from_hw(hw);
	struct bcm2835_cprman *cprman = divider->cprman;
	const struct bcm2835_pll_divider_data *data = divider->data;

	spin_lock(&cprman->regs_lock);
	cprman_write(cprman, data->a2w_reg,
		     cprman_read(cprman, data->a2w_reg) &
		     ~A2W_PLL_CHANNEL_DISABLE);

	cprman_write(cprman, data->cm_reg,
		     cprman_read(cprman, data->cm_reg) & ~data->hold_mask);
	spin_unlock(&cprman->regs_lock);

	return 0;
}

static int bcm2835_pll_divider_set_rate(struct clk_hw *hw,
					unsigned long rate,
					unsigned long parent_rate)
{
	struct bcm2835_pll_divider *divider = bcm2835_pll_divider_from_hw(hw);
	struct bcm2835_cprman *cprman = divider->cprman;
	const struct bcm2835_pll_divider_data *data = divider->data;
	u32 cm, div, max_div = 1 << A2W_PLL_DIV_BITS;

	div = DIV_ROUND_UP_ULL(parent_rate, rate);

	div = min(div, max_div);
	if (div == max_div)
		div = 0;

	cprman_write(cprman, data->a2w_reg, div);
	cm = cprman_read(cprman, data->cm_reg);
	cprman_write(cprman, data->cm_reg, cm | data->load_mask);
	cprman_write(cprman, data->cm_reg, cm & ~data->load_mask);

	return 0;
}

static const struct clk_ops bcm2835_pll_divider_clk_ops = {
	.is_prepared = bcm2835_pll_divider_is_on,
	.prepare = bcm2835_pll_divider_on,
	.unprepare = bcm2835_pll_divider_off,
	.recalc_rate = bcm2835_pll_divider_get_rate,
	.set_rate = bcm2835_pll_divider_set_rate,
	.round_rate = bcm2835_pll_divider_round_rate,
};

/*
 * The CM dividers do fixed-point division, so we can't use the
 * generic integer divider code like the PLL dividers do (and we can't
 * fake it by having some fixed shifts preceding it in the clock tree,
 * because we'd run out of bits in a 32-bit unsigned long).
 */
struct bcm2835_clock {
	struct clk_hw hw;
	struct bcm2835_cprman *cprman;
	const struct bcm2835_clock_data *data;
};

static struct bcm2835_clock *bcm2835_clock_from_hw(struct clk_hw *hw)
{
	return container_of(hw, struct bcm2835_clock, hw);
}

static int bcm2835_clock_is_on(struct clk_hw *hw)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;

	return (cprman_read(cprman, data->ctl_reg) & CM_ENABLE) != 0;
}

static u32 bcm2835_clock_choose_div(struct clk_hw *hw,
				    unsigned long rate,
				    unsigned long parent_rate)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	const struct bcm2835_clock_data *data = clock->data;
	u32 unused_frac_mask = GENMASK(CM_DIV_FRAC_BITS - data->frac_bits, 0);
	u64 temp = (u64)parent_rate << CM_DIV_FRAC_BITS;
	u32 div;

	do_div(temp, rate);
	div = temp;

	/* Round and mask off the unused bits */
	if (unused_frac_mask != 0) {
		div += unused_frac_mask >> 1;
		div &= ~unused_frac_mask;
	}

	/* clamp to min divider of 1 */
	div = max_t(u32, div, 1 << CM_DIV_FRAC_BITS);
	/* clamp to the highest possible fractional divider */
	div = min_t(u32, div, GENMASK(data->int_bits + CM_DIV_FRAC_BITS - 1,
				      CM_DIV_FRAC_BITS - data->frac_bits));

	return div;
}

static long bcm2835_clock_rate_from_divisor(struct bcm2835_clock *clock,
					    unsigned long parent_rate,
					    u32 div)
{
	const struct bcm2835_clock_data *data = clock->data;
	u64 temp;

	/*
	 * The divisor is a 12.12 fixed point field, but only some of
	 * the bits are populated in any given clock.
	 */
	div >>= CM_DIV_FRAC_BITS - data->frac_bits;
	div &= (1 << (data->int_bits + data->frac_bits)) - 1;

	if (div == 0)
		return 0;

	temp = (u64)parent_rate << data->frac_bits;

	do_div(temp, div);

	return temp;
}

static long bcm2835_clock_round_rate(struct clk_hw *hw,
				     unsigned long rate,
				     unsigned long *parent_rate)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	u32 div = bcm2835_clock_choose_div(hw, rate, *parent_rate);

	return bcm2835_clock_rate_from_divisor(clock, *parent_rate, div);
}

static unsigned long bcm2835_clock_get_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;
	u32 div = cprman_read(cprman, data->div_reg);

	return bcm2835_clock_rate_from_divisor(clock, parent_rate, div);
}

static void bcm2835_clock_wait_busy(struct bcm2835_clock *clock)
{
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;
	ktime_t timeout = ktime_add_ns(ktime_get(), LOCK_TIMEOUT_NS);

	while (cprman_read(cprman, data->ctl_reg) & CM_BUSY) {
		if (ktime_after(ktime_get(), timeout)) {
			dev_err(cprman->dev, "%s: couldn't lock PLL\n",
				clk_hw_get_name(&clock->hw));
			return;
		}
		cpu_relax();
	}
}

static void bcm2835_clock_off(struct clk_hw *hw)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;

	spin_lock(&cprman->regs_lock);
	cprman_write(cprman, data->ctl_reg,
		     cprman_read(cprman, data->ctl_reg) & ~CM_ENABLE);
	spin_unlock(&cprman->regs_lock);

	/* BUSY will remain high until the divider completes its cycle. */
	bcm2835_clock_wait_busy(clock);
}

static int bcm2835_clock_on(struct clk_hw *hw)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;

	spin_lock(&cprman->regs_lock);
	cprman_write(cprman, data->ctl_reg,
		     cprman_read(cprman, data->ctl_reg) |
		     CM_ENABLE |
		     CM_GATE);
	spin_unlock(&cprman->regs_lock);

	return 0;
}

static int bcm2835_clock_set_rate(struct clk_hw *hw,
				  unsigned long rate, unsigned long parent_rate)
{
	struct bcm2835_clock *clock = bcm2835_clock_from_hw(hw);
	struct bcm2835_cprman *cprman = clock->cprman;
	const struct bcm2835_clock_data *data = clock->data;
	u32 div = bcm2835_clock_choose_div(hw, rate, parent_rate);

	cprman_write(cprman, data->div_reg, div);

	return 0;
}

static const struct clk_ops bcm2835_clock_clk_ops = {
	.is_prepared = bcm2835_clock_is_on,
	.prepare = bcm2835_clock_on,
	.unprepare = bcm2835_clock_off,
	.recalc_rate = bcm2835_clock_get_rate,
	.set_rate = bcm2835_clock_set_rate,
	.round_rate = bcm2835_clock_round_rate,
};

static int bcm2835_vpu_clock_is_on(struct clk_hw *hw)
{
	return true;
}

/*
 * The VPU clock can never be disabled (it doesn't have an ENABLE
 * bit), so it gets its own set of clock ops.
 */
static const struct clk_ops bcm2835_vpu_clock_clk_ops = {
	.is_prepared = bcm2835_vpu_clock_is_on,
	.recalc_rate = bcm2835_clock_get_rate,
	.set_rate = bcm2835_clock_set_rate,
	.round_rate = bcm2835_clock_round_rate,
};

static struct clk *bcm2835_register_pll(struct bcm2835_cprman *cprman,
					const struct bcm2835_pll_data *data)
{
	struct bcm2835_pll *pll;
	struct clk_init_data init;

	memset(&init, 0, sizeof(init));

	/* All of the PLLs derive from the external oscillator. */
	init.parent_names = &cprman->osc_name;
	init.num_parents = 1;
	init.name = data->name;
	init.ops = &bcm2835_pll_clk_ops;
	init.flags = CLK_IGNORE_UNUSED;

	pll = kzalloc(sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return NULL;

	pll->cprman = cprman;
	pll->data = data;
	pll->hw.init = &init;

	return devm_clk_register(cprman->dev, &pll->hw);
}

static struct clk *
bcm2835_register_pll_divider(struct bcm2835_cprman *cprman,
			     const struct bcm2835_pll_divider_data *data)
{
	struct bcm2835_pll_divider *divider;
	struct clk_init_data init;
	struct clk *clk;
	const char *divider_name;

	if (data->fixed_divider != 1) {
		divider_name = devm_kasprintf(cprman->dev, GFP_KERNEL,
					      "%s_prediv", data->name);
		if (!divider_name)
			return NULL;
	} else {
		divider_name = data->name;
	}

	memset(&init, 0, sizeof(init));

	init.parent_names = &data->source_pll->name;
	init.num_parents = 1;
	init.name = divider_name;
	init.ops = &bcm2835_pll_divider_clk_ops;
	init.flags = CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED;

	divider = devm_kzalloc(cprman->dev, sizeof(*divider), GFP_KERNEL);
	if (!divider)
		return NULL;

	divider->div.reg = cprman->regs + data->a2w_reg;
	divider->div.shift = A2W_PLL_DIV_SHIFT;
	divider->div.width = A2W_PLL_DIV_BITS;
	divider->div.flags = 0;
	divider->div.lock = &cprman->regs_lock;
	divider->div.hw.init = &init;
	divider->div.table = NULL;

	divider->cprman = cprman;
	divider->data = data;

	clk = devm_clk_register(cprman->dev, &divider->div.hw);
	if (IS_ERR(clk))
		return clk;

	/*
	 * PLLH's channels have a fixed divide by 10 afterwards, which
	 * is what our consumers are actually using.
	 */
	if (data->fixed_divider != 1) {
		return clk_register_fixed_factor(cprman->dev, data->name,
						 divider_name,
						 CLK_SET_RATE_PARENT,
						 1,
						 data->fixed_divider);
	}

	return clk;
}

static struct clk *bcm2835_register_clock(struct bcm2835_cprman *cprman,
					  const struct bcm2835_clock_data *data)
{
	struct bcm2835_clock *clock;
	struct clk_init_data init;
	const char *parent;

	/*
	 * Most of the clock generators have a mux field, so we
	 * instantiate a generic mux as our parent to handle it.
	 */
	if (data->num_mux_parents) {
		const char *parents[1 << CM_SRC_BITS];
		int i;

		parent = devm_kasprintf(cprman->dev, GFP_KERNEL,
					"mux_%s", data->name);
		if (!parent)
			return NULL;

		/*
		 * Replace our "xosc" references with the oscillator's
		 * actual name.
		 */
		for (i = 0; i < data->num_mux_parents; i++) {
			if (strcmp(data->parents[i], "xosc") == 0)
				parents[i] = cprman->osc_name;
			else
				parents[i] = data->parents[i];
		}

		clk_register_mux(cprman->dev, parent,
				 parents, data->num_mux_parents,
				 CLK_SET_RATE_PARENT,
				 cprman->regs + data->ctl_reg,
				 CM_SRC_SHIFT, CM_SRC_BITS,
				 0, &cprman->regs_lock);
	} else {
		parent = data->parents[0];
	}

	memset(&init, 0, sizeof(init));
	init.parent_names = &parent;
	init.num_parents = 1;
	init.name = data->name;
	init.flags = CLK_IGNORE_UNUSED;

	if (data->is_vpu_clock) {
		init.ops = &bcm2835_vpu_clock_clk_ops;
	} else {
		init.ops = &bcm2835_clock_clk_ops;
		init.flags |= CLK_SET_RATE_GATE | CLK_SET_PARENT_GATE;
	}

	clock = devm_kzalloc(cprman->dev, sizeof(*clock), GFP_KERNEL);
	if (!clock)
		return NULL;

	clock->cprman = cprman;
	clock->data = data;
	clock->hw.init = &init;

	return devm_clk_register(cprman->dev, &clock->hw);
}

static int bcm2835_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk **clks;
	struct bcm2835_cprman *cprman;
	struct resource *res;

	cprman = devm_kzalloc(dev, sizeof(*cprman), GFP_KERNEL);
	if (!cprman)
		return -ENOMEM;

	spin_lock_init(&cprman->regs_lock);
	cprman->dev = dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cprman->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(cprman->regs))
		return PTR_ERR(cprman->regs);

	cprman->osc_name = of_clk_get_parent_name(dev->of_node, 0);
	if (!cprman->osc_name)
		return -ENODEV;

	platform_set_drvdata(pdev, cprman);

	cprman->onecell.clk_num = BCM2835_CLOCK_COUNT;
	cprman->onecell.clks = cprman->clks;
	clks = cprman->clks;

	clks[BCM2835_PLLA] = bcm2835_register_pll(cprman, &bcm2835_plla_data);
	clks[BCM2835_PLLB] = bcm2835_register_pll(cprman, &bcm2835_pllb_data);
	clks[BCM2835_PLLC] = bcm2835_register_pll(cprman, &bcm2835_pllc_data);
	clks[BCM2835_PLLD] = bcm2835_register_pll(cprman, &bcm2835_plld_data);
	clks[BCM2835_PLLH] = bcm2835_register_pll(cprman, &bcm2835_pllh_data);

	clks[BCM2835_PLLA_CORE] =
		bcm2835_register_pll_divider(cprman, &bcm2835_plla_core_data);
	clks[BCM2835_PLLA_PER] =
		bcm2835_register_pll_divider(cprman, &bcm2835_plla_per_data);
	clks[BCM2835_PLLC_CORE0] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllc_core0_data);
	clks[BCM2835_PLLC_CORE1] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllc_core1_data);
	clks[BCM2835_PLLC_CORE2] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllc_core2_data);
	clks[BCM2835_PLLC_PER] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllc_per_data);
	clks[BCM2835_PLLD_CORE] =
		bcm2835_register_pll_divider(cprman, &bcm2835_plld_core_data);
	clks[BCM2835_PLLD_PER] =
		bcm2835_register_pll_divider(cprman, &bcm2835_plld_per_data);
	clks[BCM2835_PLLH_RCAL] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllh_rcal_data);
	clks[BCM2835_PLLH_AUX] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllh_aux_data);
	clks[BCM2835_PLLH_PIX] =
		bcm2835_register_pll_divider(cprman, &bcm2835_pllh_pix_data);

	clks[BCM2835_CLOCK_TIMER] =
		bcm2835_register_clock(cprman, &bcm2835_clock_timer_data);
	clks[BCM2835_CLOCK_OTP] =
		bcm2835_register_clock(cprman, &bcm2835_clock_otp_data);
	clks[BCM2835_CLOCK_TSENS] =
		bcm2835_register_clock(cprman, &bcm2835_clock_tsens_data);
	clks[BCM2835_CLOCK_VPU] =
		bcm2835_register_clock(cprman, &bcm2835_clock_vpu_data);
	clks[BCM2835_CLOCK_V3D] =
		bcm2835_register_clock(cprman, &bcm2835_clock_v3d_data);
	clks[BCM2835_CLOCK_ISP] =
		bcm2835_register_clock(cprman, &bcm2835_clock_isp_data);
	clks[BCM2835_CLOCK_H264] =
		bcm2835_register_clock(cprman, &bcm2835_clock_h264_data);
	clks[BCM2835_CLOCK_V3D] =
		bcm2835_register_clock(cprman, &bcm2835_clock_v3d_data);
	clks[BCM2835_CLOCK_SDRAM] =
		bcm2835_register_clock(cprman, &bcm2835_clock_sdram_data);
	clks[BCM2835_CLOCK_UART] =
		bcm2835_register_clock(cprman, &bcm2835_clock_uart_data);
	clks[BCM2835_CLOCK_VEC] =
		bcm2835_register_clock(cprman, &bcm2835_clock_vec_data);
	clks[BCM2835_CLOCK_HSM] =
		bcm2835_register_clock(cprman, &bcm2835_clock_hsm_data);
	clks[BCM2835_CLOCK_EMMC] =
		bcm2835_register_clock(cprman, &bcm2835_clock_emmc_data);

	/*
	 * CM_PERIICTL (and CM_PERIACTL, CM_SYSCTL and CM_VPUCTL if
	 * you have the debug bit set in the power manager, which we
	 * don't bother exposing) are individual gates off of the
	 * non-stop vpu clock.
	 */
	clks[BCM2835_CLOCK_PERI_IMAGE] =
		clk_register_gate(dev, "peri_image", "vpu",
				  CLK_IGNORE_UNUSED | CLK_SET_RATE_GATE,
				  cprman->regs + CM_PERIICTL, CM_GATE_BIT,
				  0, &cprman->regs_lock);

	return of_clk_add_provider(dev->of_node, of_clk_src_onecell_get,
				   &cprman->onecell);
}

static const struct of_device_id bcm2835_clk_of_match[] = {
	{ .compatible = "brcm,bcm2835-cprman", },
	{}
};
MODULE_DEVICE_TABLE(of, bcm2835_clk_of_match);

static struct platform_driver bcm2835_clk_driver = {
	.driver = {
		.name = "bcm2835-clk",
		.of_match_table = bcm2835_clk_of_match,
	},
	.probe          = bcm2835_clk_probe,
};

builtin_platform_driver(bcm2835_clk_driver);

MODULE_AUTHOR("Eric Anholt <eric@anholt.net>");
MODULE_DESCRIPTION("BCM2835 clock driver");
MODULE_LICENSE("GPL v2");
