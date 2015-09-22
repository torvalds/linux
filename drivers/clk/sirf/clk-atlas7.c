/*
 * Clock tree for CSR SiRFAtlas7
 *
 * Copyright (c) 2014 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/reset-controller.h>
#include <linux/slab.h>

#define SIRFSOC_CLKC_MEMPLL_AB_FREQ          0x0000
#define SIRFSOC_CLKC_MEMPLL_AB_SSC           0x0004
#define SIRFSOC_CLKC_MEMPLL_AB_CTRL0         0x0008
#define SIRFSOC_CLKC_MEMPLL_AB_CTRL1         0x000c
#define SIRFSOC_CLKC_MEMPLL_AB_STATUS        0x0010
#define SIRFSOC_CLKC_MEMPLL_AB_SSRAM_ADDR    0x0014
#define SIRFSOC_CLKC_MEMPLL_AB_SSRAM_DATA    0x0018

#define SIRFSOC_CLKC_CPUPLL_AB_FREQ          0x001c
#define SIRFSOC_CLKC_CPUPLL_AB_SSC           0x0020
#define SIRFSOC_CLKC_CPUPLL_AB_CTRL0         0x0024
#define SIRFSOC_CLKC_CPUPLL_AB_CTRL1         0x0028
#define SIRFSOC_CLKC_CPUPLL_AB_STATUS        0x002c

#define SIRFSOC_CLKC_SYS0PLL_AB_FREQ         0x0030
#define SIRFSOC_CLKC_SYS0PLL_AB_SSC          0x0034
#define SIRFSOC_CLKC_SYS0PLL_AB_CTRL0        0x0038
#define SIRFSOC_CLKC_SYS0PLL_AB_CTRL1        0x003c
#define SIRFSOC_CLKC_SYS0PLL_AB_STATUS       0x0040

#define SIRFSOC_CLKC_SYS1PLL_AB_FREQ         0x0044
#define SIRFSOC_CLKC_SYS1PLL_AB_SSC          0x0048
#define SIRFSOC_CLKC_SYS1PLL_AB_CTRL0        0x004c
#define SIRFSOC_CLKC_SYS1PLL_AB_CTRL1        0x0050
#define SIRFSOC_CLKC_SYS1PLL_AB_STATUS       0x0054

#define SIRFSOC_CLKC_SYS2PLL_AB_FREQ         0x0058
#define SIRFSOC_CLKC_SYS2PLL_AB_SSC          0x005c
#define SIRFSOC_CLKC_SYS2PLL_AB_CTRL0        0x0060
#define SIRFSOC_CLKC_SYS2PLL_AB_CTRL1        0x0064
#define SIRFSOC_CLKC_SYS2PLL_AB_STATUS       0x0068

#define SIRFSOC_CLKC_SYS3PLL_AB_FREQ         0x006c
#define SIRFSOC_CLKC_SYS3PLL_AB_SSC          0x0070
#define SIRFSOC_CLKC_SYS3PLL_AB_CTRL0        0x0074
#define SIRFSOC_CLKC_SYS3PLL_AB_CTRL1        0x0078
#define SIRFSOC_CLKC_SYS3PLL_AB_STATUS       0x007c

#define SIRFSOC_ABPLL_CTRL0_SSEN     0x00001000
#define SIRFSOC_ABPLL_CTRL0_BYPASS   0x00000010
#define SIRFSOC_ABPLL_CTRL0_RESET    0x00000001

#define SIRFSOC_CLKC_AUDIO_DTO_INC           0x0088
#define SIRFSOC_CLKC_DISP0_DTO_INC           0x008c
#define SIRFSOC_CLKC_DISP1_DTO_INC           0x0090

#define SIRFSOC_CLKC_AUDIO_DTO_SRC           0x0094
#define SIRFSOC_CLKC_AUDIO_DTO_ENA           0x0098
#define SIRFSOC_CLKC_AUDIO_DTO_DROFF         0x009c

#define SIRFSOC_CLKC_DISP0_DTO_SRC           0x00a0
#define SIRFSOC_CLKC_DISP0_DTO_ENA           0x00a4
#define SIRFSOC_CLKC_DISP0_DTO_DROFF         0x00a8

#define SIRFSOC_CLKC_DISP1_DTO_SRC           0x00ac
#define SIRFSOC_CLKC_DISP1_DTO_ENA           0x00b0
#define SIRFSOC_CLKC_DISP1_DTO_DROFF         0x00b4

#define SIRFSOC_CLKC_I2S_CLK_SEL             0x00b8
#define SIRFSOC_CLKC_I2S_SEL_STAT            0x00bc

#define SIRFSOC_CLKC_USBPHY_CLKDIV_CFG       0x00c0
#define SIRFSOC_CLKC_USBPHY_CLKDIV_ENA       0x00c4
#define SIRFSOC_CLKC_USBPHY_CLK_SEL          0x00c8
#define SIRFSOC_CLKC_USBPHY_CLK_SEL_STAT     0x00cc

#define SIRFSOC_CLKC_BTSS_CLKDIV_CFG         0x00d0
#define SIRFSOC_CLKC_BTSS_CLKDIV_ENA         0x00d4
#define SIRFSOC_CLKC_BTSS_CLK_SEL            0x00d8
#define SIRFSOC_CLKC_BTSS_CLK_SEL_STAT       0x00dc

#define SIRFSOC_CLKC_RGMII_CLKDIV_CFG        0x00e0
#define SIRFSOC_CLKC_RGMII_CLKDIV_ENA        0x00e4
#define SIRFSOC_CLKC_RGMII_CLK_SEL           0x00e8
#define SIRFSOC_CLKC_RGMII_CLK_SEL_STAT      0x00ec

#define SIRFSOC_CLKC_CPU_CLKDIV_CFG          0x00f0
#define SIRFSOC_CLKC_CPU_CLKDIV_ENA          0x00f4
#define SIRFSOC_CLKC_CPU_CLK_SEL             0x00f8
#define SIRFSOC_CLKC_CPU_CLK_SEL_STAT        0x00fc

#define SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG      0x0100
#define SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA      0x0104
#define SIRFSOC_CLKC_SDPHY01_CLK_SEL         0x0108
#define SIRFSOC_CLKC_SDPHY01_CLK_SEL_STAT    0x010c

#define SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG      0x0110
#define SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA      0x0114
#define SIRFSOC_CLKC_SDPHY23_CLK_SEL         0x0118
#define SIRFSOC_CLKC_SDPHY23_CLK_SEL_STAT    0x011c

#define SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG      0x0120
#define SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA      0x0124
#define SIRFSOC_CLKC_SDPHY45_CLK_SEL         0x0128
#define SIRFSOC_CLKC_SDPHY45_CLK_SEL_STAT    0x012c

#define SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG      0x0130
#define SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA      0x0134
#define SIRFSOC_CLKC_SDPHY67_CLK_SEL         0x0138
#define SIRFSOC_CLKC_SDPHY67_CLK_SEL_STAT    0x013c

#define SIRFSOC_CLKC_CAN_CLKDIV_CFG          0x0140
#define SIRFSOC_CLKC_CAN_CLKDIV_ENA          0x0144
#define SIRFSOC_CLKC_CAN_CLK_SEL             0x0148
#define SIRFSOC_CLKC_CAN_CLK_SEL_STAT        0x014c

#define SIRFSOC_CLKC_DEINT_CLKDIV_CFG        0x0150
#define SIRFSOC_CLKC_DEINT_CLKDIV_ENA        0x0154
#define SIRFSOC_CLKC_DEINT_CLK_SEL           0x0158
#define SIRFSOC_CLKC_DEINT_CLK_SEL_STAT      0x015c

#define SIRFSOC_CLKC_NAND_CLKDIV_CFG         0x0160
#define SIRFSOC_CLKC_NAND_CLKDIV_ENA         0x0164
#define SIRFSOC_CLKC_NAND_CLK_SEL            0x0168
#define SIRFSOC_CLKC_NAND_CLK_SEL_STAT       0x016c

#define SIRFSOC_CLKC_DISP0_CLKDIV_CFG        0x0170
#define SIRFSOC_CLKC_DISP0_CLKDIV_ENA        0x0174
#define SIRFSOC_CLKC_DISP0_CLK_SEL           0x0178
#define SIRFSOC_CLKC_DISP0_CLK_SEL_STAT      0x017c

#define SIRFSOC_CLKC_DISP1_CLKDIV_CFG        0x0180
#define SIRFSOC_CLKC_DISP1_CLKDIV_ENA        0x0184
#define SIRFSOC_CLKC_DISP1_CLK_SEL           0x0188
#define SIRFSOC_CLKC_DISP1_CLK_SEL_STAT      0x018c

#define SIRFSOC_CLKC_GPU_CLKDIV_CFG          0x0190
#define SIRFSOC_CLKC_GPU_CLKDIV_ENA          0x0194
#define SIRFSOC_CLKC_GPU_CLK_SEL             0x0198
#define SIRFSOC_CLKC_GPU_CLK_SEL_STAT        0x019c

#define SIRFSOC_CLKC_GNSS_CLKDIV_CFG         0x01a0
#define SIRFSOC_CLKC_GNSS_CLKDIV_ENA         0x01a4
#define SIRFSOC_CLKC_GNSS_CLK_SEL            0x01a8
#define SIRFSOC_CLKC_GNSS_CLK_SEL_STAT       0x01ac

#define SIRFSOC_CLKC_SHARED_DIVIDER_CFG0     0x01b0
#define SIRFSOC_CLKC_SHARED_DIVIDER_CFG1     0x01b4
#define SIRFSOC_CLKC_SHARED_DIVIDER_ENA      0x01b8

#define SIRFSOC_CLKC_SYS_CLK_SEL             0x01bc
#define SIRFSOC_CLKC_SYS_CLK_SEL_STAT        0x01c0
#define SIRFSOC_CLKC_IO_CLK_SEL              0x01c4
#define SIRFSOC_CLKC_IO_CLK_SEL_STAT         0x01c8
#define SIRFSOC_CLKC_G2D_CLK_SEL             0x01cc
#define SIRFSOC_CLKC_G2D_CLK_SEL_STAT        0x01d0
#define SIRFSOC_CLKC_JPENC_CLK_SEL           0x01d4
#define SIRFSOC_CLKC_JPENC_CLK_SEL_STAT      0x01d8
#define SIRFSOC_CLKC_VDEC_CLK_SEL            0x01dc
#define SIRFSOC_CLKC_VDEC_CLK_SEL_STAT       0x01e0
#define SIRFSOC_CLKC_GMAC_CLK_SEL            0x01e4
#define SIRFSOC_CLKC_GMAC_CLK_SEL_STAT       0x01e8
#define SIRFSOC_CLKC_USB_CLK_SEL             0x01ec
#define SIRFSOC_CLKC_USB_CLK_SEL_STAT        0x01f0
#define SIRFSOC_CLKC_KAS_CLK_SEL             0x01f4
#define SIRFSOC_CLKC_KAS_CLK_SEL_STAT        0x01f8
#define SIRFSOC_CLKC_SEC_CLK_SEL             0x01fc
#define SIRFSOC_CLKC_SEC_CLK_SEL_STAT        0x0200
#define SIRFSOC_CLKC_SDR_CLK_SEL             0x0204
#define SIRFSOC_CLKC_SDR_CLK_SEL_STAT        0x0208
#define SIRFSOC_CLKC_VIP_CLK_SEL             0x020c
#define SIRFSOC_CLKC_VIP_CLK_SEL_STAT        0x0210
#define SIRFSOC_CLKC_NOCD_CLK_SEL            0x0214
#define SIRFSOC_CLKC_NOCD_CLK_SEL_STAT       0x0218
#define SIRFSOC_CLKC_NOCR_CLK_SEL            0x021c
#define SIRFSOC_CLKC_NOCR_CLK_SEL_STAT       0x0220
#define SIRFSOC_CLKC_TPIU_CLK_SEL            0x0224
#define SIRFSOC_CLKC_TPIU_CLK_SEL_STAT       0x0228

#define SIRFSOC_CLKC_ROOT_CLK_EN0_SET        0x022c
#define SIRFSOC_CLKC_ROOT_CLK_EN0_CLR        0x0230
#define SIRFSOC_CLKC_ROOT_CLK_EN0_STAT       0x0234
#define SIRFSOC_CLKC_ROOT_CLK_EN1_SET        0x0238
#define SIRFSOC_CLKC_ROOT_CLK_EN1_CLR        0x023c
#define SIRFSOC_CLKC_ROOT_CLK_EN1_STAT       0x0240

#define SIRFSOC_CLKC_LEAF_CLK_EN0_SET        0x0244
#define SIRFSOC_CLKC_LEAF_CLK_EN0_CLR        0x0248
#define SIRFSOC_CLKC_LEAF_CLK_EN0_STAT       0x024c

#define SIRFSOC_CLKC_RSTC_A7_SW_RST          0x0308

#define SIRFSOC_CLKC_LEAF_CLK_EN1_SET        0x04a0
#define SIRFSOC_CLKC_LEAF_CLK_EN2_SET        0x04b8
#define SIRFSOC_CLKC_LEAF_CLK_EN3_SET        0x04d0
#define SIRFSOC_CLKC_LEAF_CLK_EN4_SET        0x04e8
#define SIRFSOC_CLKC_LEAF_CLK_EN5_SET        0x0500
#define SIRFSOC_CLKC_LEAF_CLK_EN6_SET        0x0518
#define SIRFSOC_CLKC_LEAF_CLK_EN7_SET        0x0530
#define SIRFSOC_CLKC_LEAF_CLK_EN8_SET        0x0548


static void __iomem *sirfsoc_clk_vbase;
static struct clk_onecell_data clk_data;

static const struct clk_div_table pll_div_table[] = {
	{ .val = 0, .div = 1 },
	{ .val = 1, .div = 2 },
	{ .val = 2, .div = 4 },
	{ .val = 3, .div = 8 },
	{ .val = 4, .div = 16 },
	{ .val = 5, .div = 32 },
};

struct clk_pll {
	struct clk_hw hw;
	u16 regofs;  /* register offset */
};
#define to_pllclk(_hw) container_of(_hw, struct clk_pll, hw)

struct clk_dto {
	struct clk_hw hw;
	u16 inc_offset;  /* dto increment offset */
	u16 src_offset;  /* dto src offset */
};
#define to_dtoclk(_hw) container_of(_hw, struct clk_dto, hw)

struct clk_unit {
	struct clk_hw hw;
	u16 regofs;
	u16 bit;
	spinlock_t *lock;
};
#define to_unitclk(_hw) container_of(_hw, struct clk_unit, hw)

struct atlas7_div_init_data {
	const char *div_name;
	const char *parent_name;
	const char *gate_name;
	unsigned long flags;
	u8 divider_flags;
	u8 gate_flags;
	u32 div_offset;
	u8 shift;
	u8 width;
	u32 gate_offset;
	u8 gate_bit;
	spinlock_t *lock;
};

struct atlas7_mux_init_data {
	const char *mux_name;
	const char * const *parent_names;
	u8 parent_num;
	unsigned long flags;
	u8 mux_flags;
	u32 mux_offset;
	u8 shift;
	u8 width;
};

struct atlas7_unit_init_data {
	u32 index;
	const char *unit_name;
	const char *parent_name;
	unsigned long flags;
	u32 regofs;
	u8 bit;
	spinlock_t *lock;
};

struct atlas7_reset_desc {
	const char *name;
	u32 clk_ofs;
	u8  clk_bit;
	u32 rst_ofs;
	u8  rst_bit;
	spinlock_t *lock;
};

static DEFINE_SPINLOCK(cpupll_ctrl1_lock);
static DEFINE_SPINLOCK(mempll_ctrl1_lock);
static DEFINE_SPINLOCK(sys0pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys1pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys2pll_ctrl1_lock);
static DEFINE_SPINLOCK(sys3pll_ctrl1_lock);
static DEFINE_SPINLOCK(usbphy_div_lock);
static DEFINE_SPINLOCK(btss_div_lock);
static DEFINE_SPINLOCK(rgmii_div_lock);
static DEFINE_SPINLOCK(cpu_div_lock);
static DEFINE_SPINLOCK(sdphy01_div_lock);
static DEFINE_SPINLOCK(sdphy23_div_lock);
static DEFINE_SPINLOCK(sdphy45_div_lock);
static DEFINE_SPINLOCK(sdphy67_div_lock);
static DEFINE_SPINLOCK(can_div_lock);
static DEFINE_SPINLOCK(deint_div_lock);
static DEFINE_SPINLOCK(nand_div_lock);
static DEFINE_SPINLOCK(disp0_div_lock);
static DEFINE_SPINLOCK(disp1_div_lock);
static DEFINE_SPINLOCK(gpu_div_lock);
static DEFINE_SPINLOCK(gnss_div_lock);
/* gate register shared */
static DEFINE_SPINLOCK(share_div_lock);
static DEFINE_SPINLOCK(root0_gate_lock);
static DEFINE_SPINLOCK(root1_gate_lock);
static DEFINE_SPINLOCK(leaf0_gate_lock);
static DEFINE_SPINLOCK(leaf1_gate_lock);
static DEFINE_SPINLOCK(leaf2_gate_lock);
static DEFINE_SPINLOCK(leaf3_gate_lock);
static DEFINE_SPINLOCK(leaf4_gate_lock);
static DEFINE_SPINLOCK(leaf5_gate_lock);
static DEFINE_SPINLOCK(leaf6_gate_lock);
static DEFINE_SPINLOCK(leaf7_gate_lock);
static DEFINE_SPINLOCK(leaf8_gate_lock);

static inline unsigned long clkc_readl(unsigned reg)
{
	return readl(sirfsoc_clk_vbase + reg);
}

static inline void clkc_writel(u32 val, unsigned reg)
{
	writel(val, sirfsoc_clk_vbase + reg);
}

/*
*  ABPLL
*  integer mode: Fvco = Fin * 2 * NF / NR
*  Spread Spectrum mode: Fvco = Fin * SSN / NR
*  SSN = 2^24 / (256 * ((ssdiv >> ssdepth) << ssdepth) + (ssmod << ssdepth))
*/
static unsigned long pll_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long fin = parent_rate;
	struct clk_pll *clk = to_pllclk(hw);
	u64 rate;
	u32 regctrl0 = clkc_readl(clk->regofs + SIRFSOC_CLKC_MEMPLL_AB_CTRL0 -
			SIRFSOC_CLKC_MEMPLL_AB_FREQ);
	u32 regfreq = clkc_readl(clk->regofs);
	u32 regssc = clkc_readl(clk->regofs + SIRFSOC_CLKC_MEMPLL_AB_SSC -
			SIRFSOC_CLKC_MEMPLL_AB_FREQ);
	u32 nr = (regfreq  >> 16 & (BIT(3) - 1)) + 1;
	u32 nf = (regfreq & (BIT(9) - 1)) + 1;
	u32 ssdiv = regssc >> 8 & (BIT(12) - 1);
	u32 ssdepth = regssc >> 20 & (BIT(2) - 1);
	u32 ssmod = regssc & (BIT(8) - 1);

	if (regctrl0 & SIRFSOC_ABPLL_CTRL0_BYPASS)
		return fin;

	if (regctrl0 & SIRFSOC_ABPLL_CTRL0_SSEN) {
		rate = fin;
		rate *= 1 << 24;
		do_div(rate, nr);
		do_div(rate, (256 * ((ssdiv >> ssdepth) << ssdepth)
			+ (ssmod << ssdepth)));
	} else {
		rate = 2 * fin;
		rate *= nf;
		do_div(rate, nr);
	}
	return rate;
}

static const struct clk_ops ab_pll_ops = {
	.recalc_rate = pll_clk_recalc_rate,
};

static const char * const pll_clk_parents[] = {
	"xin",
};

static struct clk_init_data clk_cpupll_init = {
	.name = "cpupll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_cpupll = {
	.regofs = SIRFSOC_CLKC_CPUPLL_AB_FREQ,
	.hw = {
		.init = &clk_cpupll_init,
	},
};

static struct clk_init_data clk_mempll_init = {
	.name = "mempll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_mempll = {
	.regofs = SIRFSOC_CLKC_MEMPLL_AB_FREQ,
	.hw = {
		.init = &clk_mempll_init,
	},
};

static struct clk_init_data clk_sys0pll_init = {
	.name = "sys0pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys0pll = {
	.regofs = SIRFSOC_CLKC_SYS0PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys0pll_init,
	},
};

static struct clk_init_data clk_sys1pll_init = {
	.name = "sys1pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys1pll = {
	.regofs = SIRFSOC_CLKC_SYS1PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys1pll_init,
	},
};

static struct clk_init_data clk_sys2pll_init = {
	.name = "sys2pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys2pll = {
	.regofs = SIRFSOC_CLKC_SYS2PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys2pll_init,
	},
};

static struct clk_init_data clk_sys3pll_init = {
	.name = "sys3pll_vco",
	.ops = &ab_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_sys3pll = {
	.regofs = SIRFSOC_CLKC_SYS3PLL_AB_FREQ,
	.hw = {
		.init = &clk_sys3pll_init,
	},
};

/*
 *  DTO in clkc, default enable double resolution mode
 *  double resolution mode:fout = fin * finc / 2^29
 *  normal mode:fout = fin * finc / 2^28
 */
#define DTO_RESL_DOUBLE	(1ULL << 29)
#define DTO_RESL_NORMAL	(1ULL << 28)

static int dto_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_dto *clk = to_dtoclk(hw);
	int reg;

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	return !!(clkc_readl(reg) & BIT(0));
}

static int dto_clk_enable(struct clk_hw *hw)
{
	u32 val, reg;
	struct clk_dto *clk = to_dtoclk(hw);

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	val = clkc_readl(reg) | BIT(0);
	clkc_writel(val, reg);
	return 0;
}

static void dto_clk_disable(struct clk_hw *hw)
{
	u32 val, reg;
	struct clk_dto *clk = to_dtoclk(hw);

	reg = clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_ENA - SIRFSOC_CLKC_AUDIO_DTO_SRC;

	val = clkc_readl(reg) & ~BIT(0);
	clkc_writel(val, reg);
}

static unsigned long dto_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	u64 rate = parent_rate;
	struct clk_dto *clk = to_dtoclk(hw);
	u32 finc = clkc_readl(clk->inc_offset);
	u32 droff = clkc_readl(clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_DROFF - SIRFSOC_CLKC_AUDIO_DTO_SRC);

	rate *= finc;
	if (droff & BIT(0))
		/* Double resolution off */
		do_div(rate, DTO_RESL_NORMAL);
	else
		do_div(rate, DTO_RESL_DOUBLE);

	return rate;
}

static long dto_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	u64 dividend = rate * DTO_RESL_DOUBLE;

	do_div(dividend, *parent_rate);
	dividend *= *parent_rate;
	do_div(dividend, DTO_RESL_DOUBLE);

	return dividend;
}

static int dto_clk_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	u64 dividend = rate * DTO_RESL_DOUBLE;
	struct clk_dto *clk = to_dtoclk(hw);

	do_div(dividend, parent_rate);
	clkc_writel(0, clk->src_offset + SIRFSOC_CLKC_AUDIO_DTO_DROFF - SIRFSOC_CLKC_AUDIO_DTO_SRC);
	clkc_writel(dividend, clk->inc_offset);

	return 0;
}

static u8 dto_clk_get_parent(struct clk_hw *hw)
{
	struct clk_dto *clk = to_dtoclk(hw);

	return clkc_readl(clk->src_offset);
}

/*
 *   dto need CLK_SET_PARENT_GATE
 */
static int dto_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_dto *clk = to_dtoclk(hw);

	clkc_writel(index, clk->src_offset);
	return 0;
}

static const struct clk_ops dto_ops = {
	.is_enabled = dto_clk_is_enabled,
	.enable = dto_clk_enable,
	.disable = dto_clk_disable,
	.recalc_rate = dto_clk_recalc_rate,
	.round_rate = dto_clk_round_rate,
	.set_rate = dto_clk_set_rate,
	.get_parent = dto_clk_get_parent,
	.set_parent = dto_clk_set_parent,
};

/* dto parent clock as syspllvco/clk1 */
static const char * const audiodto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_audiodto_init = {
	.name = "audio_dto",
	.ops = &dto_ops,
	.parent_names = audiodto_clk_parents,
	.num_parents = ARRAY_SIZE(audiodto_clk_parents),
};

static struct clk_dto clk_audio_dto = {
	.inc_offset = SIRFSOC_CLKC_AUDIO_DTO_INC,
	.src_offset = SIRFSOC_CLKC_AUDIO_DTO_SRC,
	.hw = {
		.init = &clk_audiodto_init,
	},
};

static const char * const disp0dto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_disp0dto_init = {
	.name = "disp0_dto",
	.ops = &dto_ops,
	.parent_names = disp0dto_clk_parents,
	.num_parents = ARRAY_SIZE(disp0dto_clk_parents),
};

static struct clk_dto clk_disp0_dto = {
	.inc_offset = SIRFSOC_CLKC_DISP0_DTO_INC,
	.src_offset = SIRFSOC_CLKC_DISP0_DTO_SRC,
	.hw = {
		.init = &clk_disp0dto_init,
	},
};

static const char * const disp1dto_clk_parents[] = {
	"sys0pll_clk1",
	"sys1pll_clk1",
	"sys3pll_clk1",
};

static struct clk_init_data clk_disp1dto_init = {
	.name = "disp1_dto",
	.ops = &dto_ops,
	.parent_names = disp1dto_clk_parents,
	.num_parents = ARRAY_SIZE(disp1dto_clk_parents),
};

static struct clk_dto clk_disp1_dto = {
	.inc_offset = SIRFSOC_CLKC_DISP1_DTO_INC,
	.src_offset = SIRFSOC_CLKC_DISP1_DTO_SRC,
	.hw = {
		.init = &clk_disp1dto_init,
	},
};

static struct atlas7_div_init_data divider_list[] __initdata = {
	/* div_name, parent_name, gate_name, clk_flag, divider_flag, gate_flag, div_offset, shift, wdith, gate_offset, bit_enable, lock */
	{ "sys0pll_qa1", "sys0pll_fixdiv", "sys0pll_a1", 0, 0, 0, SIRFSOC_CLKC_USBPHY_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_USBPHY_CLKDIV_ENA, 0, &usbphy_div_lock },
	{ "sys1pll_qa1", "sys1pll_fixdiv", "sys1pll_a1", 0, 0, 0, SIRFSOC_CLKC_USBPHY_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_USBPHY_CLKDIV_ENA, 4, &usbphy_div_lock },
	{ "sys2pll_qa1", "sys2pll_fixdiv", "sys2pll_a1", 0, 0, 0, SIRFSOC_CLKC_USBPHY_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_USBPHY_CLKDIV_ENA, 8, &usbphy_div_lock },
	{ "sys3pll_qa1", "sys3pll_fixdiv", "sys3pll_a1", 0, 0, 0, SIRFSOC_CLKC_USBPHY_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_USBPHY_CLKDIV_ENA, 12, &usbphy_div_lock },
	{ "sys0pll_qa2", "sys0pll_fixdiv", "sys0pll_a2", 0, 0, 0, SIRFSOC_CLKC_BTSS_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_BTSS_CLKDIV_ENA, 0, &btss_div_lock },
	{ "sys1pll_qa2", "sys1pll_fixdiv", "sys1pll_a2", 0, 0, 0, SIRFSOC_CLKC_BTSS_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_BTSS_CLKDIV_ENA, 4, &btss_div_lock },
	{ "sys2pll_qa2", "sys2pll_fixdiv", "sys2pll_a2", 0, 0, 0, SIRFSOC_CLKC_BTSS_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_BTSS_CLKDIV_ENA, 8, &btss_div_lock },
	{ "sys3pll_qa2", "sys3pll_fixdiv", "sys3pll_a2", 0, 0, 0, SIRFSOC_CLKC_BTSS_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_BTSS_CLKDIV_ENA, 12, &btss_div_lock },
	{ "sys0pll_qa3", "sys0pll_fixdiv", "sys0pll_a3", 0, 0, 0, SIRFSOC_CLKC_RGMII_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_RGMII_CLKDIV_ENA, 0, &rgmii_div_lock },
	{ "sys1pll_qa3", "sys1pll_fixdiv", "sys1pll_a3", 0, 0, 0, SIRFSOC_CLKC_RGMII_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_RGMII_CLKDIV_ENA, 4, &rgmii_div_lock },
	{ "sys2pll_qa3", "sys2pll_fixdiv", "sys2pll_a3", 0, 0, 0, SIRFSOC_CLKC_RGMII_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_RGMII_CLKDIV_ENA, 8, &rgmii_div_lock },
	{ "sys3pll_qa3", "sys3pll_fixdiv", "sys3pll_a3", 0, 0, 0, SIRFSOC_CLKC_RGMII_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_RGMII_CLKDIV_ENA, 12, &rgmii_div_lock },
	{ "sys0pll_qa4", "sys0pll_fixdiv", "sys0pll_a4", 0, 0, 0, SIRFSOC_CLKC_CPU_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_CPU_CLKDIV_ENA, 0, &cpu_div_lock },
	{ "sys1pll_qa4", "sys1pll_fixdiv", "sys1pll_a4", 0, 0, CLK_IGNORE_UNUSED, SIRFSOC_CLKC_CPU_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_CPU_CLKDIV_ENA, 4, &cpu_div_lock },
	{ "sys0pll_qa5", "sys0pll_fixdiv", "sys0pll_a5", 0, 0, 0, SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA, 0, &sdphy01_div_lock },
	{ "sys1pll_qa5", "sys1pll_fixdiv", "sys1pll_a5", 0, 0, 0, SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA, 4, &sdphy01_div_lock },
	{ "sys2pll_qa5", "sys2pll_fixdiv", "sys2pll_a5", 0, 0, 0, SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA, 8, &sdphy01_div_lock },
	{ "sys3pll_qa5", "sys3pll_fixdiv", "sys3pll_a5", 0, 0, 0, SIRFSOC_CLKC_SDPHY01_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_SDPHY01_CLKDIV_ENA, 12, &sdphy01_div_lock },
	{ "sys0pll_qa6", "sys0pll_fixdiv", "sys0pll_a6", 0, 0, 0, SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA, 0, &sdphy23_div_lock },
	{ "sys1pll_qa6", "sys1pll_fixdiv", "sys1pll_a6", 0, 0, 0, SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA, 4, &sdphy23_div_lock },
	{ "sys2pll_qa6", "sys2pll_fixdiv", "sys2pll_a6", 0, 0, 0, SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA, 8, &sdphy23_div_lock },
	{ "sys3pll_qa6", "sys3pll_fixdiv", "sys3pll_a6", 0, 0, 0, SIRFSOC_CLKC_SDPHY23_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_SDPHY23_CLKDIV_ENA, 12, &sdphy23_div_lock },
	{ "sys0pll_qa7", "sys0pll_fixdiv", "sys0pll_a7", 0, 0, 0, SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA, 0, &sdphy45_div_lock },
	{ "sys1pll_qa7", "sys1pll_fixdiv", "sys1pll_a7", 0, 0, 0, SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA, 4, &sdphy45_div_lock },
	{ "sys2pll_qa7", "sys2pll_fixdiv", "sys2pll_a7", 0, 0, 0, SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA, 8, &sdphy45_div_lock },
	{ "sys3pll_qa7", "sys3pll_fixdiv", "sys3pll_a7", 0, 0, 0, SIRFSOC_CLKC_SDPHY45_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_SDPHY45_CLKDIV_ENA, 12, &sdphy45_div_lock },
	{ "sys0pll_qa8", "sys0pll_fixdiv", "sys0pll_a8", 0, 0, 0, SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA, 0, &sdphy67_div_lock },
	{ "sys1pll_qa8", "sys1pll_fixdiv", "sys1pll_a8", 0, 0, 0, SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA, 4, &sdphy67_div_lock },
	{ "sys2pll_qa8", "sys2pll_fixdiv", "sys2pll_a8", 0, 0, 0, SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA, 8, &sdphy67_div_lock },
	{ "sys3pll_qa8", "sys3pll_fixdiv", "sys3pll_a8", 0, 0, 0, SIRFSOC_CLKC_SDPHY67_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_SDPHY67_CLKDIV_ENA, 12, &sdphy67_div_lock },
	{ "sys0pll_qa9", "sys0pll_fixdiv", "sys0pll_a9", 0, 0, 0, SIRFSOC_CLKC_CAN_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_CAN_CLKDIV_ENA, 0, &can_div_lock },
	{ "sys1pll_qa9", "sys1pll_fixdiv", "sys1pll_a9", 0, 0, 0, SIRFSOC_CLKC_CAN_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_CAN_CLKDIV_ENA, 4, &can_div_lock },
	{ "sys2pll_qa9", "sys2pll_fixdiv", "sys2pll_a9", 0, 0, 0, SIRFSOC_CLKC_CAN_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_CAN_CLKDIV_ENA, 8, &can_div_lock },
	{ "sys3pll_qa9", "sys3pll_fixdiv", "sys3pll_a9", 0, 0, 0, SIRFSOC_CLKC_CAN_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_CAN_CLKDIV_ENA, 12, &can_div_lock },
	{ "sys0pll_qa10", "sys0pll_fixdiv", "sys0pll_a10", 0, 0, 0, SIRFSOC_CLKC_DEINT_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_DEINT_CLKDIV_ENA, 0, &deint_div_lock },
	{ "sys1pll_qa10", "sys1pll_fixdiv", "sys1pll_a10", 0, 0, 0, SIRFSOC_CLKC_DEINT_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_DEINT_CLKDIV_ENA, 4, &deint_div_lock },
	{ "sys2pll_qa10", "sys2pll_fixdiv", "sys2pll_a10", 0, 0, 0, SIRFSOC_CLKC_DEINT_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_DEINT_CLKDIV_ENA, 8, &deint_div_lock },
	{ "sys3pll_qa10", "sys3pll_fixdiv", "sys3pll_a10", 0, 0, 0, SIRFSOC_CLKC_DEINT_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_DEINT_CLKDIV_ENA, 12, &deint_div_lock },
	{ "sys0pll_qa11", "sys0pll_fixdiv", "sys0pll_a11", 0, 0, 0, SIRFSOC_CLKC_NAND_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_NAND_CLKDIV_ENA, 0, &nand_div_lock },
	{ "sys1pll_qa11", "sys1pll_fixdiv", "sys1pll_a11", 0, 0, 0, SIRFSOC_CLKC_NAND_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_NAND_CLKDIV_ENA, 4, &nand_div_lock },
	{ "sys2pll_qa11", "sys2pll_fixdiv", "sys2pll_a11", 0, 0, 0, SIRFSOC_CLKC_NAND_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_NAND_CLKDIV_ENA, 8, &nand_div_lock },
	{ "sys3pll_qa11", "sys3pll_fixdiv", "sys3pll_a11", 0, 0, 0, SIRFSOC_CLKC_NAND_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_NAND_CLKDIV_ENA, 12, &nand_div_lock },
	{ "sys0pll_qa12", "sys0pll_fixdiv", "sys0pll_a12", 0, 0, 0, SIRFSOC_CLKC_DISP0_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_DISP0_CLKDIV_ENA, 0, &disp0_div_lock },
	{ "sys1pll_qa12", "sys1pll_fixdiv", "sys1pll_a12", 0, 0, 0, SIRFSOC_CLKC_DISP0_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_DISP0_CLKDIV_ENA, 4, &disp0_div_lock },
	{ "sys2pll_qa12", "sys2pll_fixdiv", "sys2pll_a12", 0, 0, 0, SIRFSOC_CLKC_DISP0_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_DISP0_CLKDIV_ENA, 8, &disp0_div_lock },
	{ "sys3pll_qa12", "sys3pll_fixdiv", "sys3pll_a12", 0, 0, 0, SIRFSOC_CLKC_DISP0_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_DISP0_CLKDIV_ENA, 12, &disp0_div_lock },
	{ "sys0pll_qa13", "sys0pll_fixdiv", "sys0pll_a13", 0, 0, 0, SIRFSOC_CLKC_DISP1_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_DISP1_CLKDIV_ENA, 0, &disp1_div_lock },
	{ "sys1pll_qa13", "sys1pll_fixdiv", "sys1pll_a13", 0, 0, 0, SIRFSOC_CLKC_DISP1_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_DISP1_CLKDIV_ENA, 4, &disp1_div_lock },
	{ "sys2pll_qa13", "sys2pll_fixdiv", "sys2pll_a13", 0, 0, 0, SIRFSOC_CLKC_DISP1_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_DISP1_CLKDIV_ENA, 8, &disp1_div_lock },
	{ "sys3pll_qa13", "sys3pll_fixdiv", "sys3pll_a13", 0, 0, 0, SIRFSOC_CLKC_DISP1_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_DISP1_CLKDIV_ENA, 12, &disp1_div_lock },
	{ "sys0pll_qa14", "sys0pll_fixdiv", "sys0pll_a14", 0, 0, 0, SIRFSOC_CLKC_GPU_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_GPU_CLKDIV_ENA, 0, &gpu_div_lock },
	{ "sys1pll_qa14", "sys1pll_fixdiv", "sys1pll_a14", 0, 0, 0, SIRFSOC_CLKC_GPU_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_GPU_CLKDIV_ENA, 4, &gpu_div_lock },
	{ "sys2pll_qa14", "sys2pll_fixdiv", "sys2pll_a14", 0, 0, 0, SIRFSOC_CLKC_GPU_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_GPU_CLKDIV_ENA, 8, &gpu_div_lock },
	{ "sys3pll_qa14", "sys3pll_fixdiv", "sys3pll_a14", 0, 0, 0, SIRFSOC_CLKC_GPU_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_GPU_CLKDIV_ENA, 12, &gpu_div_lock },
	{ "sys0pll_qa15", "sys0pll_fixdiv", "sys0pll_a15", 0, 0, 0, SIRFSOC_CLKC_GNSS_CLKDIV_CFG, 0, 6, SIRFSOC_CLKC_GNSS_CLKDIV_ENA, 0, &gnss_div_lock },
	{ "sys1pll_qa15", "sys1pll_fixdiv", "sys1pll_a15", 0, 0, 0, SIRFSOC_CLKC_GNSS_CLKDIV_CFG, 8, 6, SIRFSOC_CLKC_GNSS_CLKDIV_ENA, 4, &gnss_div_lock },
	{ "sys2pll_qa15", "sys2pll_fixdiv", "sys2pll_a15", 0, 0, 0, SIRFSOC_CLKC_GNSS_CLKDIV_CFG, 16, 6, SIRFSOC_CLKC_GNSS_CLKDIV_ENA, 8, &gnss_div_lock },
	{ "sys3pll_qa15", "sys3pll_fixdiv", "sys3pll_a15", 0, 0, 0, SIRFSOC_CLKC_GNSS_CLKDIV_CFG, 24, 6, SIRFSOC_CLKC_GNSS_CLKDIV_ENA, 12, &gnss_div_lock },
	{ "sys1pll_qa18", "sys1pll_fixdiv", "sys1pll_a18", 0, 0, 0, SIRFSOC_CLKC_SHARED_DIVIDER_CFG0, 24, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 12, &share_div_lock },
	{ "sys1pll_qa19", "sys1pll_fixdiv", "sys1pll_a19", 0, 0, CLK_IGNORE_UNUSED, SIRFSOC_CLKC_SHARED_DIVIDER_CFG0, 16, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 8, &share_div_lock },
	{ "sys1pll_qa20", "sys1pll_fixdiv", "sys1pll_a20", 0, 0, 0, SIRFSOC_CLKC_SHARED_DIVIDER_CFG0, 8, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 4, &share_div_lock },
	{ "sys2pll_qa20", "sys2pll_fixdiv", "sys2pll_a20", 0, 0, 0, SIRFSOC_CLKC_SHARED_DIVIDER_CFG0, 0, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 0, &share_div_lock },
	{ "sys1pll_qa17", "sys1pll_fixdiv", "sys1pll_a17", 0, 0, CLK_IGNORE_UNUSED, SIRFSOC_CLKC_SHARED_DIVIDER_CFG1, 8, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 20, &share_div_lock },
	{ "sys0pll_qa20", "sys0pll_fixdiv", "sys0pll_a20", 0, 0, 0, SIRFSOC_CLKC_SHARED_DIVIDER_CFG1, 0, 6, SIRFSOC_CLKC_SHARED_DIVIDER_ENA, 16, &share_div_lock },
};

static const char * const i2s_clk_parents[] = {
	"xin",
	"xinw",
	"audio_dto",
	/* "pwm_i2s01" */
};

static const char * const usbphy_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a1",
	"sys1pll_a1",
	"sys2pll_a1",
	"sys3pll_a1",
};

static const char * const btss_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a2",
	"sys1pll_a2",
	"sys2pll_a2",
	"sys3pll_a2",
};

static const char * const rgmii_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a3",
	"sys1pll_a3",
	"sys2pll_a3",
	"sys3pll_a3",
};

static const char * const cpu_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a4",
	"sys1pll_a4",
	"cpupll_clk1",
};

static const char * const sdphy01_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a5",
	"sys1pll_a5",
	"sys2pll_a5",
	"sys3pll_a5",
};

static const char * const sdphy23_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a6",
	"sys1pll_a6",
	"sys2pll_a6",
	"sys3pll_a6",
};

static const char * const sdphy45_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a7",
	"sys1pll_a7",
	"sys2pll_a7",
	"sys3pll_a7",
};

static const char * const sdphy67_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a8",
	"sys1pll_a8",
	"sys2pll_a8",
	"sys3pll_a8",
};

static const char * const can_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a9",
	"sys1pll_a9",
	"sys2pll_a9",
	"sys3pll_a9",
};

static const char * const deint_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a10",
	"sys1pll_a10",
	"sys2pll_a10",
	"sys3pll_a10",
};

static const char * const nand_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a11",
	"sys1pll_a11",
	"sys2pll_a11",
	"sys3pll_a11",
};

static const char * const disp0_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a12",
	"sys1pll_a12",
	"sys2pll_a12",
	"sys3pll_a12",
	"disp0_dto",
};

static const char * const disp1_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a13",
	"sys1pll_a13",
	"sys2pll_a13",
	"sys3pll_a13",
	"disp1_dto",
};

static const char * const gpu_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a14",
	"sys1pll_a14",
	"sys2pll_a14",
	"sys3pll_a14",
};

static const char * const gnss_clk_parents[] = {
	"xin",
	"xinw",
	"sys0pll_a15",
	"sys1pll_a15",
	"sys2pll_a15",
	"sys3pll_a15",
};

static const char * const sys_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const io_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const g2d_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const jpenc_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const vdec_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const gmac_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const usb_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const kas_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const sec_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const sdr_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const vip_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const nocd_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const nocr_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static const char * const tpiu_clk_parents[] = {
	"xin",
	"xinw",
	"sys2pll_a20",
	"sys1pll_a20",
	"sys1pll_a19",
	"sys1pll_a18",
	"sys0pll_a20",
	"sys1pll_a17",
};

static struct atlas7_mux_init_data mux_list[] __initdata = {
	/* mux_name, parent_names, parent_num, flags, mux_flags, mux_offset, shift, width */
	{ "i2s_mux", i2s_clk_parents, ARRAY_SIZE(i2s_clk_parents), 0, 0, SIRFSOC_CLKC_I2S_CLK_SEL, 0, 2 },
	{ "usbphy_mux", usbphy_clk_parents, ARRAY_SIZE(usbphy_clk_parents), 0, 0, SIRFSOC_CLKC_I2S_CLK_SEL, 0, 3 },
	{ "btss_mux", btss_clk_parents, ARRAY_SIZE(btss_clk_parents), 0, 0, SIRFSOC_CLKC_BTSS_CLK_SEL, 0, 3 },
	{ "rgmii_mux", rgmii_clk_parents, ARRAY_SIZE(rgmii_clk_parents), 0, 0, SIRFSOC_CLKC_RGMII_CLK_SEL, 0, 3 },
	{ "cpu_mux", cpu_clk_parents, ARRAY_SIZE(cpu_clk_parents), 0, 0, SIRFSOC_CLKC_CPU_CLK_SEL, 0, 3 },
	{ "sdphy01_mux", sdphy01_clk_parents, ARRAY_SIZE(sdphy01_clk_parents), 0, 0, SIRFSOC_CLKC_SDPHY01_CLK_SEL, 0, 3 },
	{ "sdphy23_mux", sdphy23_clk_parents, ARRAY_SIZE(sdphy23_clk_parents), 0, 0, SIRFSOC_CLKC_SDPHY23_CLK_SEL, 0, 3 },
	{ "sdphy45_mux", sdphy45_clk_parents, ARRAY_SIZE(sdphy45_clk_parents), 0, 0, SIRFSOC_CLKC_SDPHY45_CLK_SEL, 0, 3 },
	{ "sdphy67_mux", sdphy67_clk_parents, ARRAY_SIZE(sdphy67_clk_parents), 0, 0, SIRFSOC_CLKC_SDPHY67_CLK_SEL, 0, 3 },
	{ "can_mux", can_clk_parents, ARRAY_SIZE(can_clk_parents), 0, 0, SIRFSOC_CLKC_CAN_CLK_SEL, 0, 3 },
	{ "deint_mux", deint_clk_parents, ARRAY_SIZE(deint_clk_parents), 0, 0, SIRFSOC_CLKC_DEINT_CLK_SEL, 0, 3 },
	{ "nand_mux", nand_clk_parents, ARRAY_SIZE(nand_clk_parents), 0, 0, SIRFSOC_CLKC_NAND_CLK_SEL, 0, 3 },
	{ "disp0_mux", disp0_clk_parents, ARRAY_SIZE(disp0_clk_parents), 0, 0, SIRFSOC_CLKC_DISP0_CLK_SEL, 0, 3 },
	{ "disp1_mux", disp1_clk_parents, ARRAY_SIZE(disp1_clk_parents), 0, 0, SIRFSOC_CLKC_DISP1_CLK_SEL, 0, 3 },
	{ "gpu_mux", gpu_clk_parents, ARRAY_SIZE(gpu_clk_parents), 0, 0, SIRFSOC_CLKC_GPU_CLK_SEL, 0, 3 },
	{ "gnss_mux", gnss_clk_parents, ARRAY_SIZE(gnss_clk_parents), 0, 0, SIRFSOC_CLKC_GNSS_CLK_SEL, 0, 3 },
	{ "sys_mux", sys_clk_parents, ARRAY_SIZE(sys_clk_parents), 0, 0, SIRFSOC_CLKC_SYS_CLK_SEL, 0, 3 },
	{ "io_mux", io_clk_parents, ARRAY_SIZE(io_clk_parents), 0, 0, SIRFSOC_CLKC_IO_CLK_SEL, 0, 3 },
	{ "g2d_mux", g2d_clk_parents, ARRAY_SIZE(g2d_clk_parents), 0, 0, SIRFSOC_CLKC_G2D_CLK_SEL, 0, 3 },
	{ "jpenc_mux", jpenc_clk_parents, ARRAY_SIZE(jpenc_clk_parents), 0, 0, SIRFSOC_CLKC_JPENC_CLK_SEL, 0, 3 },
	{ "vdec_mux", vdec_clk_parents, ARRAY_SIZE(vdec_clk_parents), 0, 0, SIRFSOC_CLKC_VDEC_CLK_SEL, 0, 3 },
	{ "gmac_mux", gmac_clk_parents, ARRAY_SIZE(gmac_clk_parents), 0, 0, SIRFSOC_CLKC_GMAC_CLK_SEL, 0, 3 },
	{ "usb_mux", usb_clk_parents, ARRAY_SIZE(usb_clk_parents), 0, 0, SIRFSOC_CLKC_USB_CLK_SEL, 0, 3 },
	{ "kas_mux", kas_clk_parents, ARRAY_SIZE(kas_clk_parents), 0, 0, SIRFSOC_CLKC_KAS_CLK_SEL, 0, 3 },
	{ "sec_mux", sec_clk_parents, ARRAY_SIZE(sec_clk_parents), 0, 0, SIRFSOC_CLKC_SEC_CLK_SEL, 0, 3 },
	{ "sdr_mux", sdr_clk_parents, ARRAY_SIZE(sdr_clk_parents), 0, 0, SIRFSOC_CLKC_SDR_CLK_SEL, 0, 3 },
	{ "vip_mux", vip_clk_parents, ARRAY_SIZE(vip_clk_parents), 0, 0, SIRFSOC_CLKC_VIP_CLK_SEL, 0, 3 },
	{ "nocd_mux", nocd_clk_parents, ARRAY_SIZE(nocd_clk_parents), 0, 0, SIRFSOC_CLKC_NOCD_CLK_SEL, 0, 3 },
	{ "nocr_mux", nocr_clk_parents, ARRAY_SIZE(nocr_clk_parents), 0, 0, SIRFSOC_CLKC_NOCR_CLK_SEL, 0, 3 },
	{ "tpiu_mux", tpiu_clk_parents, ARRAY_SIZE(tpiu_clk_parents), 0, 0, SIRFSOC_CLKC_TPIU_CLK_SEL, 0, 3 },
};

	/* new unit should add start from the tail of list */
static struct atlas7_unit_init_data unit_list[] __initdata = {
	/* unit_name, parent_name, flags, regofs, bit, lock */
	{ 0, "audmscm_kas", "kas_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 0, &root0_gate_lock },
	{ 1, "gnssm_gnss", "gnss_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 1, &root0_gate_lock },
	{ 2, "gpum_gpu", "gpu_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 2, &root0_gate_lock },
	{ 3, "mediam_g2d", "g2d_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 3, &root0_gate_lock },
	{ 4, "mediam_jpenc", "jpenc_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 4, &root0_gate_lock },
	{ 5, "vdifm_disp0", "disp0_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 5, &root0_gate_lock },
	{ 6, "vdifm_disp1", "disp1_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 6, &root0_gate_lock },
	{ 7, "audmscm_i2s", "i2s_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 8, &root0_gate_lock },
	{ 8, "audmscm_io", "io_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 11, &root0_gate_lock },
	{ 9, "vdifm_io", "io_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 12, &root0_gate_lock },
	{ 10, "gnssm_io", "io_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 13, &root0_gate_lock },
	{ 11, "mediam_io", "io_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 14, &root0_gate_lock },
	{ 12, "btm_io", "io_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 17, &root0_gate_lock },
	{ 13, "mediam_sdphy01", "sdphy01_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 18, &root0_gate_lock },
	{ 14, "vdifm_sdphy23", "sdphy23_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 19, &root0_gate_lock },
	{ 15, "vdifm_sdphy45", "sdphy45_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 20, &root0_gate_lock },
	{ 16, "vdifm_sdphy67", "sdphy67_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 21, &root0_gate_lock },
	{ 17, "audmscm_xin", "xin", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 22, &root0_gate_lock },
	{ 18, "mediam_nand", "nand_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 27, &root0_gate_lock },
	{ 19, "gnssm_sec", "sec_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 28, &root0_gate_lock },
	{ 20, "cpum_cpu", "cpu_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 29, &root0_gate_lock },
	{ 21, "gnssm_xin", "xin", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 30, &root0_gate_lock },
	{ 22, "vdifm_vip", "vip_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN0_SET, 31, &root0_gate_lock },
	{ 23, "btm_btss", "btss_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 0, &root1_gate_lock },
	{ 24, "mediam_usbphy", "usbphy_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 1, &root1_gate_lock },
	{ 25, "rtcm_kas", "kas_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 2, &root1_gate_lock },
	{ 26, "audmscm_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 3, &root1_gate_lock },
	{ 27, "vdifm_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 4, &root1_gate_lock },
	{ 28, "gnssm_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 5, &root1_gate_lock },
	{ 29, "mediam_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 6, &root1_gate_lock },
	{ 30, "cpum_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 8, &root1_gate_lock },
	{ 31, "gpum_nocd", "nocd_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 9, &root1_gate_lock },
	{ 32, "audmscm_nocr", "nocr_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 11, &root1_gate_lock },
	{ 33, "vdifm_nocr", "nocr_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 12, &root1_gate_lock },
	{ 34, "gnssm_nocr", "nocr_mux", CLK_IGNORE_UNUSED, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 13, &root1_gate_lock },
	{ 35, "mediam_nocr", "nocr_mux", CLK_IGNORE_UNUSED, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 14, &root1_gate_lock },
	{ 36, "ddrm_nocr", "nocr_mux", CLK_IGNORE_UNUSED, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 15, &root1_gate_lock },
	{ 37, "cpum_tpiu", "tpiu_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 16, &root1_gate_lock },
	{ 38, "gpum_nocr", "nocr_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 17, &root1_gate_lock },
	{ 39, "gnssm_rgmii", "rgmii_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 20, &root1_gate_lock },
	{ 40, "mediam_vdec", "vdec_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 21, &root1_gate_lock },
	{ 41, "gpum_sdr", "sdr_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 22, &root1_gate_lock },
	{ 42, "vdifm_deint", "deint_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 23, &root1_gate_lock },
	{ 43, "gnssm_can", "can_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 26, &root1_gate_lock },
	{ 44, "mediam_usb", "usb_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 28, &root1_gate_lock },
	{ 45, "gnssm_gmac", "gmac_mux", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 29, &root1_gate_lock },
	{ 46, "cvd_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 0, &leaf1_gate_lock },
	{ 47, "timer_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 1, &leaf1_gate_lock },
	{ 48, "pulse_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 2, &leaf1_gate_lock },
	{ 49, "tsc_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 3, &leaf1_gate_lock },
	{ 50, "tsc_xin", "audmscm_xin", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 21, &leaf1_gate_lock },
	{ 51, "ioctop_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 4, &leaf1_gate_lock },
	{ 52, "rsc_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 5, &leaf1_gate_lock },
	{ 53, "dvm_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 6, &leaf1_gate_lock },
	{ 54, "lvds_xin", "audmscm_xin", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 7, &leaf1_gate_lock },
	{ 55, "kas_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 8, &leaf1_gate_lock },
	{ 56, "ac97_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 9, &leaf1_gate_lock },
	{ 57, "usp0_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 10, &leaf1_gate_lock },
	{ 58, "usp1_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 11, &leaf1_gate_lock },
	{ 59, "usp2_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 12, &leaf1_gate_lock },
	{ 60, "dmac2_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 13, &leaf1_gate_lock },
	{ 61, "dmac3_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 14, &leaf1_gate_lock },
	{ 62, "audioif_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 15, &leaf1_gate_lock },
	{ 63, "i2s1_kas", "audmscm_kas", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 17, &leaf1_gate_lock },
	{ 64, "thaudmscm_io", "audmscm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 22, &leaf1_gate_lock },
	{ 65, "analogtest_xin", "audmscm_xin", 0, SIRFSOC_CLKC_LEAF_CLK_EN1_SET, 23, &leaf1_gate_lock },
	{ 66, "sys2pci_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 0, &leaf2_gate_lock },
	{ 67, "pciarb_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 1, &leaf2_gate_lock },
	{ 68, "pcicopy_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 2, &leaf2_gate_lock },
	{ 69, "rom_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 3, &leaf2_gate_lock },
	{ 70, "sdio23_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 4, &leaf2_gate_lock },
	{ 71, "sdio45_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 5, &leaf2_gate_lock },
	{ 72, "sdio67_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 6, &leaf2_gate_lock },
	{ 73, "vip1_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 7, &leaf2_gate_lock },
	{ 74, "vip1_vip", "vdifm_vip", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 16, &leaf2_gate_lock },
	{ 75, "sdio23_sdphy23", "vdifm_sdphy23", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 8, &leaf2_gate_lock },
	{ 76, "sdio45_sdphy45", "vdifm_sdphy45", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 9, &leaf2_gate_lock },
	{ 77, "sdio67_sdphy67", "vdifm_sdphy67", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 10, &leaf2_gate_lock },
	{ 78, "vpp0_disp0", "vdifm_disp0", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 11, &leaf2_gate_lock },
	{ 79, "lcd0_disp0", "vdifm_disp0", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 12, &leaf2_gate_lock },
	{ 80, "vpp1_disp1", "vdifm_disp1", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 13, &leaf2_gate_lock },
	{ 81, "lcd1_disp1", "vdifm_disp1", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 14, &leaf2_gate_lock },
	{ 82, "dcu_deint", "vdifm_deint", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 15, &leaf2_gate_lock },
	{ 83, "vdifm_dapa_r_nocr", "vdifm_nocr", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 17, &leaf2_gate_lock },
	{ 84, "gpio1_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 18, &leaf2_gate_lock },
	{ 85, "thvdifm_io", "vdifm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN2_SET, 19, &leaf2_gate_lock },
	{ 86, "gmac_rgmii", "gnssm_rgmii", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 0, &leaf3_gate_lock },
	{ 87, "gmac_gmac", "gnssm_gmac", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 1, &leaf3_gate_lock },
	{ 88, "uart1_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 2, &leaf3_gate_lock },
	{ 89, "dmac0_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 3, &leaf3_gate_lock },
	{ 90, "uart0_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 4, &leaf3_gate_lock },
	{ 91, "uart2_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 5, &leaf3_gate_lock },
	{ 92, "uart3_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 6, &leaf3_gate_lock },
	{ 93, "uart4_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 7, &leaf3_gate_lock },
	{ 94, "uart5_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 8, &leaf3_gate_lock },
	{ 95, "spi1_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 9, &leaf3_gate_lock },
	{ 96, "gnss_gnss", "gnssm_gnss", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 10, &leaf3_gate_lock },
	{ 97, "canbus1_can", "gnssm_can", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 12, &leaf3_gate_lock },
	{ 98, "ccsec_sec", "gnssm_sec", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 15, &leaf3_gate_lock },
	{ 99,  "ccpub_sec", "gnssm_sec", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 16, &leaf3_gate_lock },
	{ 100, "gnssm_dapa_r_nocr", "gnssm_nocr", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 13, &leaf3_gate_lock },
	{ 101, "thgnssm_io", "gnssm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN3_SET, 14, &leaf3_gate_lock },
	{ 102, "media_vdec", "mediam_vdec", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 0, &leaf4_gate_lock },
	{ 103, "media_jpenc", "mediam_jpenc", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 1, &leaf4_gate_lock },
	{ 104, "g2d_g2d", "mediam_g2d", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 2, &leaf4_gate_lock },
	{ 105, "i2c0_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 3, &leaf4_gate_lock },
	{ 106, "i2c1_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 4, &leaf4_gate_lock },
	{ 107, "gpio0_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 5, &leaf4_gate_lock },
	{ 108, "nand_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 6, &leaf4_gate_lock },
	{ 109, "sdio01_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 7, &leaf4_gate_lock },
	{ 110, "sys2pci2_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 8, &leaf4_gate_lock },
	{ 111, "sdio01_sdphy01", "mediam_sdphy01", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 9, &leaf4_gate_lock },
	{ 112, "nand_nand", "mediam_nand", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 10, &leaf4_gate_lock },
	{ 113, "usb0_usb", "mediam_usb", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 11, &leaf4_gate_lock },
	{ 114, "usb1_usb", "mediam_usb", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 12, &leaf4_gate_lock },
	{ 115, "usbphy0_usbphy", "mediam_usbphy", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 13, &leaf4_gate_lock },
	{ 116, "usbphy1_usbphy", "mediam_usbphy", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 14, &leaf4_gate_lock },
	{ 117, "thmediam_io", "mediam_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN4_SET, 15, &leaf4_gate_lock },
	{ 118, "memc_mem", "mempll_clk1", CLK_IGNORE_UNUSED, SIRFSOC_CLKC_LEAF_CLK_EN5_SET, 0, &leaf5_gate_lock },
	{ 119, "dapa_mem", "mempll_clk1", 0, SIRFSOC_CLKC_LEAF_CLK_EN5_SET, 1, &leaf5_gate_lock },
	{ 120, "nocddrm_nocr", "ddrm_nocr", 0, SIRFSOC_CLKC_LEAF_CLK_EN5_SET, 2, &leaf5_gate_lock },
	{ 121, "thddrm_nocr", "ddrm_nocr", 0, SIRFSOC_CLKC_LEAF_CLK_EN5_SET, 3, &leaf5_gate_lock },
	{ 122, "spram1_cpudiv2", "cpum_cpu", 0, SIRFSOC_CLKC_LEAF_CLK_EN6_SET, 0, &leaf6_gate_lock },
	{ 123, "spram2_cpudiv2", "cpum_cpu", 0, SIRFSOC_CLKC_LEAF_CLK_EN6_SET, 1, &leaf6_gate_lock },
	{ 124, "coresight_cpudiv2", "cpum_cpu", 0, SIRFSOC_CLKC_LEAF_CLK_EN6_SET, 2, &leaf6_gate_lock },
	{ 125, "coresight_tpiu", "cpum_tpiu", 0, SIRFSOC_CLKC_LEAF_CLK_EN6_SET, 3, &leaf6_gate_lock },
	{ 126, "graphic_gpu", "gpum_gpu", 0, SIRFSOC_CLKC_LEAF_CLK_EN7_SET, 0, &leaf7_gate_lock },
	{ 127, "vss_sdr", "gpum_sdr", 0, SIRFSOC_CLKC_LEAF_CLK_EN7_SET, 1, &leaf7_gate_lock },
	{ 128, "thgpum_nocr", "gpum_nocr", 0, SIRFSOC_CLKC_LEAF_CLK_EN7_SET, 2, &leaf7_gate_lock },
	{ 129, "a7ca_btss", "btm_btss", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 1, &leaf8_gate_lock },
	{ 130, "dmac4_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 2, &leaf8_gate_lock },
	{ 131, "uart6_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 3, &leaf8_gate_lock },
	{ 132, "usp3_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 4, &leaf8_gate_lock },
	{ 133, "a7ca_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 5, &leaf8_gate_lock },
	{ 134, "noc_btm_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 6, &leaf8_gate_lock },
	{ 135, "thbtm_io", "btm_io", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 7, &leaf8_gate_lock },
	{ 136, "btslow", "xinw_fixdiv_btslow", 0, SIRFSOC_CLKC_ROOT_CLK_EN1_SET, 25, &root1_gate_lock },
	{ 137, "a7ca_btslow", "btslow", 0, SIRFSOC_CLKC_LEAF_CLK_EN8_SET, 0, &leaf8_gate_lock },
	{ 138, "pwm_io", "io_mux", 0, SIRFSOC_CLKC_LEAF_CLK_EN0_SET, 0, &leaf0_gate_lock },
	{ 139, "pwm_xin", "xin", 0, SIRFSOC_CLKC_LEAF_CLK_EN0_SET, 1, &leaf0_gate_lock },
	{ 140, "pwm_xinw", "xinw", 0, SIRFSOC_CLKC_LEAF_CLK_EN0_SET, 2, &leaf0_gate_lock },
	{ 141, "thcgum_sys", "sys_mux", 0, SIRFSOC_CLKC_LEAF_CLK_EN0_SET, 3, &leaf0_gate_lock },
};

static struct clk *atlas7_clks[ARRAY_SIZE(unit_list) + ARRAY_SIZE(mux_list)];

static int unit_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_unit *clk = to_unitclk(hw);
	u32 reg;

	reg = clk->regofs + SIRFSOC_CLKC_ROOT_CLK_EN0_STAT - SIRFSOC_CLKC_ROOT_CLK_EN0_SET;

	return !!(clkc_readl(reg) & BIT(clk->bit));
}

static int unit_clk_enable(struct clk_hw *hw)
{
	u32 reg;
	struct clk_unit *clk = to_unitclk(hw);
	unsigned long flags;

	reg = clk->regofs;

	spin_lock_irqsave(clk->lock, flags);
	clkc_writel(BIT(clk->bit), reg);
	spin_unlock_irqrestore(clk->lock, flags);
	return 0;
}

static void unit_clk_disable(struct clk_hw *hw)
{
	u32  reg;
	struct clk_unit *clk = to_unitclk(hw);
	unsigned long flags;

	reg = clk->regofs + SIRFSOC_CLKC_ROOT_CLK_EN0_CLR - SIRFSOC_CLKC_ROOT_CLK_EN0_SET;

	spin_lock_irqsave(clk->lock, flags);
	clkc_writel(BIT(clk->bit), reg);
	spin_unlock_irqrestore(clk->lock, flags);
}

static const struct clk_ops unit_clk_ops = {
	.is_enabled = unit_clk_is_enabled,
	.enable = unit_clk_enable,
	.disable = unit_clk_disable,
};

static struct clk * __init
atlas7_unit_clk_register(struct device *dev, const char *name,
		 const char * const parent_name, unsigned long flags,
		 u32 regofs, u8 bit, spinlock_t *lock)
{
	struct clk *clk;
	struct clk_unit *unit;
	struct clk_init_data init;

	unit = kzalloc(sizeof(*unit), GFP_KERNEL);
	if (!unit)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = &parent_name;
	init.num_parents = 1;
	init.ops = &unit_clk_ops;
	init.flags = flags;

	unit->hw.init = &init;
	unit->regofs = regofs;
	unit->bit = bit;
	unit->lock = lock;

	clk = clk_register(dev, &unit->hw);
	if (IS_ERR(clk))
		kfree(unit);

	return clk;
}

static struct atlas7_reset_desc atlas7_reset_unit[] = {
	{ "PWM", 0x0244, 0, 0x0320, 0, &leaf0_gate_lock }, /* 0-5 */
	{ "THCGUM", 0x0244, 3, 0x0320, 1, &leaf0_gate_lock },
	{ "CVD", 0x04A0, 0, 0x032C, 0, &leaf1_gate_lock },
	{ "TIMER", 0x04A0, 1, 0x032C, 1, &leaf1_gate_lock },
	{ "PULSEC", 0x04A0, 2, 0x032C, 2, &leaf1_gate_lock },
	{ "TSC", 0x04A0, 3, 0x032C, 3, &leaf1_gate_lock },
	{ "IOCTOP", 0x04A0, 4, 0x032C, 4, &leaf1_gate_lock }, /* 6-10 */
	{ "RSC", 0x04A0, 5, 0x032C, 5, &leaf1_gate_lock },
	{ "DVM", 0x04A0, 6, 0x032C, 6, &leaf1_gate_lock },
	{ "LVDS", 0x04A0, 7, 0x032C, 7, &leaf1_gate_lock },
	{ "KAS", 0x04A0, 8, 0x032C, 8, &leaf1_gate_lock },
	{ "AC97", 0x04A0, 9, 0x032C, 9, &leaf1_gate_lock }, /* 11-15 */
	{ "USP0", 0x04A0, 10, 0x032C, 10, &leaf1_gate_lock },
	{ "USP1", 0x04A0, 11, 0x032C, 11, &leaf1_gate_lock },
	{ "USP2", 0x04A0, 12, 0x032C, 12, &leaf1_gate_lock },
	{ "DMAC2", 0x04A0, 13, 0x032C, 13, &leaf1_gate_lock },
	{ "DMAC3", 0x04A0, 14, 0x032C, 14, &leaf1_gate_lock }, /* 16-20 */
	{ "AUDIO", 0x04A0, 15, 0x032C, 15, &leaf1_gate_lock },
	{ "I2S1", 0x04A0, 17, 0x032C, 16, &leaf1_gate_lock },
	{ "PMU_AUDIO", 0x04A0, 22, 0x032C, 17, &leaf1_gate_lock },
	{ "THAUDMSCM", 0x04A0, 23, 0x032C, 18, &leaf1_gate_lock },
	{ "SYS2PCI", 0x04B8, 0, 0x0338, 0, &leaf2_gate_lock }, /* 21-25 */
	{ "PCIARB", 0x04B8, 1, 0x0338, 1, &leaf2_gate_lock },
	{ "PCICOPY", 0x04B8, 2, 0x0338, 2, &leaf2_gate_lock },
	{ "ROM", 0x04B8, 3, 0x0338, 3, &leaf2_gate_lock },
	{ "SDIO23", 0x04B8, 4, 0x0338, 4, &leaf2_gate_lock },
	{ "SDIO45", 0x04B8, 5, 0x0338, 5, &leaf2_gate_lock }, /* 26-30 */
	{ "SDIO67", 0x04B8, 6, 0x0338, 6, &leaf2_gate_lock },
	{ "VIP1", 0x04B8, 7, 0x0338, 7, &leaf2_gate_lock },
	{ "VPP0", 0x04B8, 11, 0x0338, 8, &leaf2_gate_lock },
	{ "LCD0", 0x04B8, 12, 0x0338, 9, &leaf2_gate_lock },
	{ "VPP1", 0x04B8, 13, 0x0338, 10, &leaf2_gate_lock }, /* 31-35 */
	{ "LCD1", 0x04B8, 14, 0x0338, 11, &leaf2_gate_lock },
	{ "DCU", 0x04B8, 15, 0x0338, 12, &leaf2_gate_lock },
	{ "GPIO", 0x04B8, 18, 0x0338, 13, &leaf2_gate_lock },
	{ "DAPA_VDIFM", 0x04B8, 17, 0x0338, 15, &leaf2_gate_lock },
	{ "THVDIFM", 0x04B8, 19, 0x0338, 16, &leaf2_gate_lock }, /* 36-40 */
	{ "RGMII", 0x04D0, 0, 0x0344, 0, &leaf3_gate_lock },
	{ "GMAC", 0x04D0, 1, 0x0344, 1, &leaf3_gate_lock },
	{ "UART1", 0x04D0, 2, 0x0344, 2, &leaf3_gate_lock },
	{ "DMAC0", 0x04D0, 3, 0x0344, 3, &leaf3_gate_lock },
	{ "UART0", 0x04D0, 4, 0x0344, 4, &leaf3_gate_lock }, /* 41-45 */
	{ "UART2", 0x04D0, 5, 0x0344, 5, &leaf3_gate_lock },
	{ "UART3", 0x04D0, 6, 0x0344, 6, &leaf3_gate_lock },
	{ "UART4", 0x04D0, 7, 0x0344, 7, &leaf3_gate_lock },
	{ "UART5", 0x04D0, 8, 0x0344, 8, &leaf3_gate_lock },
	{ "SPI1", 0x04D0, 9, 0x0344, 9, &leaf3_gate_lock }, /* 46-50 */
	{ "GNSS_SYS_M0", 0x04D0, 10, 0x0344, 10, &leaf3_gate_lock },
	{ "CANBUS1", 0x04D0, 12, 0x0344, 11, &leaf3_gate_lock },
	{ "CCSEC", 0x04D0, 15, 0x0344, 12, &leaf3_gate_lock },
	{ "CCPUB", 0x04D0, 16, 0x0344, 13, &leaf3_gate_lock },
	{ "DAPA_GNSSM", 0x04D0, 13, 0x0344, 14, &leaf3_gate_lock }, /* 51-55 */
	{ "THGNSSM", 0x04D0, 14, 0x0344, 15, &leaf3_gate_lock },
	{ "VDEC", 0x04E8, 0, 0x0350, 0, &leaf4_gate_lock },
	{ "JPENC", 0x04E8, 1, 0x0350, 1, &leaf4_gate_lock },
	{ "G2D", 0x04E8, 2, 0x0350, 2, &leaf4_gate_lock },
	{ "I2C0", 0x04E8, 3, 0x0350, 3, &leaf4_gate_lock }, /* 56-60 */
	{ "I2C1", 0x04E8, 4, 0x0350, 4, &leaf4_gate_lock },
	{ "GPIO0", 0x04E8, 5, 0x0350, 5, &leaf4_gate_lock },
	{ "NAND", 0x04E8, 6, 0x0350, 6, &leaf4_gate_lock },
	{ "SDIO01", 0x04E8, 7, 0x0350, 7, &leaf4_gate_lock },
	{ "SYS2PCI2", 0x04E8, 8, 0x0350, 8, &leaf4_gate_lock }, /* 61-65 */
	{ "USB0", 0x04E8, 11, 0x0350, 9, &leaf4_gate_lock },
	{ "USB1", 0x04E8, 12, 0x0350, 10, &leaf4_gate_lock },
	{ "THMEDIAM", 0x04E8, 15, 0x0350, 11, &leaf4_gate_lock },
	{ "MEMC_DDRPHY", 0x0500, 0, 0x035C, 0, &leaf5_gate_lock },
	{ "MEMC_UPCTL", 0x0500, 0, 0x035C, 1, &leaf5_gate_lock }, /* 66-70 */
	{ "DAPA_MEM", 0x0500, 1, 0x035C, 2, &leaf5_gate_lock },
	{ "MEMC_MEMDIV", 0x0500, 0, 0x035C, 3, &leaf5_gate_lock },
	{ "THDDRM", 0x0500, 3, 0x035C, 4, &leaf5_gate_lock },
	{ "CORESIGHT", 0x0518, 3, 0x0368, 13, &leaf6_gate_lock },
	{ "THCPUM", 0x0518, 4, 0x0368, 17, &leaf6_gate_lock }, /* 71-75 */
	{ "GRAPHIC", 0x0530, 0, 0x0374, 0, &leaf7_gate_lock },
	{ "VSS_SDR", 0x0530, 1, 0x0374, 1, &leaf7_gate_lock },
	{ "THGPUM", 0x0530, 2, 0x0374, 2, &leaf7_gate_lock },
	{ "DMAC4", 0x0548, 2, 0x0380, 1, &leaf8_gate_lock },
	{ "UART6", 0x0548, 3, 0x0380, 2, &leaf8_gate_lock }, /* 76- */
	{ "USP3", 0x0548, 4, 0x0380, 3, &leaf8_gate_lock },
	{ "THBTM", 0x0548, 5, 0x0380, 5, &leaf8_gate_lock },
	{ "A7CA", 0x0548, 1, 0x0380, 0, &leaf8_gate_lock },
	{ "A7CA_APB", 0x0548, 5, 0x0380, 4, &leaf8_gate_lock },
};

static int atlas7_reset_module(struct reset_controller_dev *rcdev,
					unsigned long reset_idx)
{
	struct atlas7_reset_desc *reset = &atlas7_reset_unit[reset_idx];
	unsigned long flags;

	/*
	 * HW suggest unit reset sequence:
	 * assert sw reset (0)
	 * setting sw clk_en to if the clock was disabled before reset
	 * delay 16 clocks
	 * disable clock (sw clk_en = 0)
	 * de-assert reset (1)
	 * after this sequence, restore clock or not is decided by SW
	 */

	spin_lock_irqsave(reset->lock, flags);
	/* clock enable or not */
	if (clkc_readl(reset->clk_ofs + 8) & (1 << reset->clk_bit)) {
		clkc_writel(1 << reset->rst_bit, reset->rst_ofs + 4);
		udelay(2);
		clkc_writel(1 << reset->clk_bit, reset->clk_ofs + 4);
		clkc_writel(1 << reset->rst_bit, reset->rst_ofs);
		/* restore clock enable */
		clkc_writel(1 << reset->clk_bit, reset->clk_ofs);
	} else {
		clkc_writel(1 << reset->rst_bit, reset->rst_ofs + 4);
		clkc_writel(1 << reset->clk_bit, reset->clk_ofs);
		udelay(2);
		clkc_writel(1 << reset->clk_bit, reset->clk_ofs + 4);
		clkc_writel(1 << reset->rst_bit, reset->rst_ofs);
	}
	spin_unlock_irqrestore(reset->lock, flags);

	return 0;
}

static struct reset_control_ops atlas7_rst_ops = {
	.reset = atlas7_reset_module,
};

static struct reset_controller_dev atlas7_rst_ctlr = {
	.ops = &atlas7_rst_ops,
	.owner = THIS_MODULE,
	.of_reset_n_cells = 1,
};

static void __init atlas7_clk_init(struct device_node *np)
{
	struct clk *clk;
	struct atlas7_div_init_data *div;
	struct atlas7_mux_init_data *mux;
	struct atlas7_unit_init_data *unit;
	int i;
	int ret;

	sirfsoc_clk_vbase = of_iomap(np, 0);
	if (!sirfsoc_clk_vbase)
		panic("unable to map clkc registers\n");

	of_node_put(np);

	clk = clk_register(NULL, &clk_cpupll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_mempll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys0pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys1pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys2pll.hw);
	BUG_ON(!clk);
	clk = clk_register(NULL, &clk_sys3pll.hw);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "cpupll_div1", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "cpupll_div2", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "cpupll_div3", "cpupll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &cpupll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "mempll_div1", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "mempll_div2", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "mempll_div3", "mempll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &mempll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_divider_table(NULL, "sys0pll_div1", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys0pll_div2", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys0pll_div3", "sys0pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys0pll_fixdiv", "sys0pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys1pll_div1", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys1pll_div2", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys1pll_div3", "sys1pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys1pll_fixdiv", "sys1pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys2pll_div1", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys2pll_div2", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys2pll_div3", "sys2pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys2pll_fixdiv", "sys2pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	clk = clk_register_divider_table(NULL, "sys3pll_div1", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 0, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys3pll_div2", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 4, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_divider_table(NULL, "sys3pll_div3", "sys3pll_vco", 0,
			 sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1, 8, 3, 0,
			 pll_div_table, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "sys3pll_fixdiv", "sys3pll_vco",
					CLK_SET_RATE_PARENT, 1, 2);

	BUG_ON(!clk);
	clk = clk_register_fixed_factor(NULL, "xinw_fixdiv_btslow", "xinw",
					CLK_SET_RATE_PARENT, 1, 4);

	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk1", "cpupll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				12, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk2", "cpupll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				13, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "cpupll_clk3", "cpupll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_CPUPLL_AB_CTRL1,
				14, 0, &cpupll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "mempll_clk1", "mempll_div1",
		CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
		sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
		12, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "mempll_clk2", "mempll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
				13, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "mempll_clk3", "mempll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_MEMPLL_AB_CTRL1,
				14, 0, &mempll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys0pll_clk1", "sys0pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				12, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys0pll_clk2", "sys0pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				13, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys0pll_clk3", "sys0pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS0PLL_AB_CTRL1,
				14, 0, &sys0pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys1pll_clk1", "sys1pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				12, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys1pll_clk2", "sys1pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				13, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys1pll_clk3", "sys1pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS1PLL_AB_CTRL1,
				14, 0, &sys1pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys2pll_clk1", "sys2pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				12, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys2pll_clk2", "sys2pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				13, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys2pll_clk3", "sys2pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS2PLL_AB_CTRL1,
				14, 0, &sys2pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register_gate(NULL, "sys3pll_clk1", "sys3pll_div1",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				12, 0, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys3pll_clk2", "sys3pll_div2",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				13, 0, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);
	clk = clk_register_gate(NULL, "sys3pll_clk3", "sys3pll_div3",
		CLK_SET_RATE_PARENT, sirfsoc_clk_vbase + SIRFSOC_CLKC_SYS3PLL_AB_CTRL1,
				14, 0, &sys3pll_ctrl1_lock);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_audio_dto.hw);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_disp0_dto.hw);
	BUG_ON(!clk);

	clk = clk_register(NULL, &clk_disp1_dto.hw);
	BUG_ON(!clk);

	for (i = 0; i < ARRAY_SIZE(divider_list); i++) {
		div = &divider_list[i];
		clk = clk_register_divider(NULL, div->div_name,
			div->parent_name, div->divider_flags, sirfsoc_clk_vbase + div->div_offset,
			div->shift, div->width, 0, div->lock);
		BUG_ON(!clk);
		clk = clk_register_gate(NULL, div->gate_name, div->div_name,
			div->gate_flags, sirfsoc_clk_vbase + div->gate_offset,
				div->gate_bit, 0, div->lock);
		BUG_ON(!clk);
	}
	/* ignore selector status register check */
	for (i = 0; i < ARRAY_SIZE(mux_list); i++) {
		mux = &mux_list[i];
		clk = clk_register_mux(NULL, mux->mux_name, mux->parent_names,
			       mux->parent_num, mux->flags,
			       sirfsoc_clk_vbase + mux->mux_offset,
			       mux->shift, mux->width,
			       mux->mux_flags, NULL);
		atlas7_clks[ARRAY_SIZE(unit_list) + i] = clk;
		BUG_ON(!clk);
	}

	for (i = 0; i < ARRAY_SIZE(unit_list); i++) {
		unit = &unit_list[i];
		atlas7_clks[i] = atlas7_unit_clk_register(NULL, unit->unit_name, unit->parent_name,
				unit->flags, unit->regofs, unit->bit, unit->lock);
		BUG_ON(!atlas7_clks[i]);
	}

	clk_data.clks = atlas7_clks;
	clk_data.clk_num = ARRAY_SIZE(unit_list) + ARRAY_SIZE(mux_list);

	ret = of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
	BUG_ON(ret);

	atlas7_rst_ctlr.of_node = np;
	atlas7_rst_ctlr.nr_resets = ARRAY_SIZE(atlas7_reset_unit);
	reset_controller_register(&atlas7_rst_ctlr);
}
CLK_OF_DECLARE(atlas7_clk, "sirf,atlas7-car", atlas7_clk_init);
