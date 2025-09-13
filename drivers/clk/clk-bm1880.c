// SPDX-License-Identifier: GPL-2.0+
/*
 * Bitmain BM1880 SoC clock driver
 *
 * Copyright (c) 2019 Linaro Ltd.
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <dt-bindings/clock/bm1880-clock.h>

#define BM1880_CLK_MPLL_CTL	0x00
#define BM1880_CLK_SPLL_CTL	0x04
#define BM1880_CLK_FPLL_CTL	0x08
#define BM1880_CLK_DDRPLL_CTL	0x0c

#define BM1880_CLK_ENABLE0	0x00
#define BM1880_CLK_ENABLE1	0x04
#define BM1880_CLK_SELECT	0x20
#define BM1880_CLK_DIV0		0x40
#define BM1880_CLK_DIV1		0x44
#define BM1880_CLK_DIV2		0x48
#define BM1880_CLK_DIV3		0x4c
#define BM1880_CLK_DIV4		0x50
#define BM1880_CLK_DIV5		0x54
#define BM1880_CLK_DIV6		0x58
#define BM1880_CLK_DIV7		0x5c
#define BM1880_CLK_DIV8		0x60
#define BM1880_CLK_DIV9		0x64
#define BM1880_CLK_DIV10	0x68
#define BM1880_CLK_DIV11	0x6c
#define BM1880_CLK_DIV12	0x70
#define BM1880_CLK_DIV13	0x74
#define BM1880_CLK_DIV14	0x78
#define BM1880_CLK_DIV15	0x7c
#define BM1880_CLK_DIV16	0x80
#define BM1880_CLK_DIV17	0x84
#define BM1880_CLK_DIV18	0x88
#define BM1880_CLK_DIV19	0x8c
#define BM1880_CLK_DIV20	0x90
#define BM1880_CLK_DIV21	0x94
#define BM1880_CLK_DIV22	0x98
#define BM1880_CLK_DIV23	0x9c
#define BM1880_CLK_DIV24	0xa0
#define BM1880_CLK_DIV25	0xa4
#define BM1880_CLK_DIV26	0xa8
#define BM1880_CLK_DIV27	0xac
#define BM1880_CLK_DIV28	0xb0

#define to_bm1880_pll_clk(_hw) container_of(_hw, struct bm1880_pll_hw_clock, hw)
#define to_bm1880_div_clk(_hw) container_of(_hw, struct bm1880_div_hw_clock, hw)

static DEFINE_SPINLOCK(bm1880_clk_lock);

struct bm1880_clock_data {
	void __iomem *pll_base;
	void __iomem *sys_base;
	struct clk_hw_onecell_data hw_data;
};

struct bm1880_gate_clock {
	unsigned int	id;
	const char	*name;
	const char      *parent;
	u32		gate_reg;
	s8		gate_shift;
	unsigned long	flags;
};

struct bm1880_mux_clock {
	unsigned int	id;
	const char	*name;
	const char      * const *parents;
	s8		num_parents;
	u32		reg;
	s8		shift;
	unsigned long	flags;
};

struct bm1880_div_clock {
	unsigned int	id;
	const char	*name;
	u32		reg;
	u8		shift;
	u8		width;
	u32		initval;
	const struct clk_div_table *table;
	unsigned long flags;
};

struct bm1880_div_hw_clock {
	struct bm1880_div_clock div;
	void __iomem *base;
	spinlock_t *lock;
	struct clk_hw hw;
	struct clk_init_data init;
};

struct bm1880_composite_clock {
	unsigned int	id;
	const char	*name;
	const char	*parent;
	const char      * const *parents;
	unsigned int	num_parents;
	unsigned long	flags;

	u32		gate_reg;
	u32		mux_reg;
	u32		div_reg;

	s8		gate_shift;
	s8		mux_shift;
	s8		div_shift;
	s8		div_width;
	s16		div_initval;
	const struct clk_div_table *table;
};

struct bm1880_pll_clock {
	unsigned int	id;
	const char	*name;
	u32		reg;
	unsigned long	flags;
};

struct bm1880_pll_hw_clock {
	struct bm1880_pll_clock pll;
	void __iomem *base;
	struct clk_hw hw;
	struct clk_init_data init;
};

static const struct clk_ops bm1880_pll_ops;
static const struct clk_ops bm1880_clk_div_ops;

#define GATE_DIV(_id, _name, _parent, _gate_reg, _gate_shift, _div_reg,	\
			_div_shift, _div_width, _div_initval, _table,	\
			_flags) {					\
		.id = _id,						\
		.parent = _parent,					\
		.name = _name,						\
		.gate_reg = _gate_reg,					\
		.gate_shift = _gate_shift,				\
		.div_reg = _div_reg,					\
		.div_shift = _div_shift,				\
		.div_width = _div_width,				\
		.div_initval = _div_initval,				\
		.table = _table,					\
		.mux_shift = -1,					\
		.flags = _flags,					\
	}

#define GATE_MUX(_id, _name, _parents, _gate_reg, _gate_shift,		\
			_mux_reg, _mux_shift, _flags) {			\
		.id = _id,						\
		.parents = _parents,					\
		.num_parents = ARRAY_SIZE(_parents),			\
		.name = _name,						\
		.gate_reg = _gate_reg,					\
		.gate_shift = _gate_shift,				\
		.div_shift = -1,					\
		.mux_reg = _mux_reg,					\
		.mux_shift = _mux_shift,				\
		.flags = _flags,					\
	}

#define CLK_PLL(_id, _name, _parent, _reg, _flags) {			\
		.pll.id = _id,						\
		.pll.name = _name,					\
		.pll.reg = _reg,					\
		.hw.init = CLK_HW_INIT_PARENTS_DATA(_name, _parent,	\
						    &bm1880_pll_ops,	\
						    _flags),		\
	}

#define CLK_DIV(_id, _name, _parent, _reg, _shift, _width, _initval,	\
				_table,	_flags) {			\
		.div.id = _id,						\
		.div.name = _name,					\
		.div.reg = _reg,					\
		.div.shift = _shift,					\
		.div.width = _width,					\
		.div.initval = _initval,				\
		.div.table = _table,					\
		.hw.init = CLK_HW_INIT_HW(_name, _parent,		\
					  &bm1880_clk_div_ops,		\
					  _flags),			\
	}

static struct clk_parent_data bm1880_pll_parent[] = {
	{ .fw_name = "osc", .name = "osc" },
};

/*
 * All PLL clocks are marked as CRITICAL, hence they are very crucial
 * for the functioning of the SoC
 */
static struct bm1880_pll_hw_clock bm1880_pll_clks[] = {
	CLK_PLL(BM1880_CLK_MPLL, "clk_mpll", bm1880_pll_parent,
		BM1880_CLK_MPLL_CTL, 0),
	CLK_PLL(BM1880_CLK_SPLL, "clk_spll", bm1880_pll_parent,
		BM1880_CLK_SPLL_CTL, 0),
	CLK_PLL(BM1880_CLK_FPLL, "clk_fpll", bm1880_pll_parent,
		BM1880_CLK_FPLL_CTL, 0),
	CLK_PLL(BM1880_CLK_DDRPLL, "clk_ddrpll", bm1880_pll_parent,
		BM1880_CLK_DDRPLL_CTL, 0),
};

/*
 * Clocks marked as CRITICAL are needed for the proper functioning
 * of the SoC.
 */
static const struct bm1880_gate_clock bm1880_gate_clks[] = {
	{ BM1880_CLK_AHB_ROM, "clk_ahb_rom", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 2, 0 },
	{ BM1880_CLK_AXI_SRAM, "clk_axi_sram", "clk_axi1",
	  BM1880_CLK_ENABLE0, 3, 0 },
	/*
	 * Since this clock is sourcing the DDR memory, let's mark it as
	 * critical to avoid gating.
	 */
	{ BM1880_CLK_DDR_AXI, "clk_ddr_axi", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 4, CLK_IS_CRITICAL },
	{ BM1880_CLK_APB_EFUSE, "clk_apb_efuse", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 6, 0 },
	{ BM1880_CLK_AXI5_EMMC, "clk_axi5_emmc", "clk_axi5",
	  BM1880_CLK_ENABLE0, 7, 0 },
	{ BM1880_CLK_AXI5_SD, "clk_axi5_sd", "clk_axi5",
	  BM1880_CLK_ENABLE0, 10, 0 },
	{ BM1880_CLK_AXI4_ETH0, "clk_axi4_eth0", "clk_axi4",
	  BM1880_CLK_ENABLE0, 14, 0 },
	{ BM1880_CLK_AXI4_ETH1, "clk_axi4_eth1", "clk_axi4",
	  BM1880_CLK_ENABLE0, 16, 0 },
	{ BM1880_CLK_AXI1_GDMA, "clk_axi1_gdma", "clk_axi1",
	  BM1880_CLK_ENABLE0, 17, 0 },
	/* Don't gate GPIO clocks as it is not owned by the GPIO driver */
	{ BM1880_CLK_APB_GPIO, "clk_apb_gpio", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 18, CLK_IGNORE_UNUSED },
	{ BM1880_CLK_APB_GPIO_INTR, "clk_apb_gpio_intr", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 19, CLK_IGNORE_UNUSED },
	{ BM1880_CLK_AXI1_MINER, "clk_axi1_miner", "clk_axi1",
	  BM1880_CLK_ENABLE0, 21, 0 },
	{ BM1880_CLK_AHB_SF, "clk_ahb_sf", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 22, 0 },
	/*
	 * Not sure which module this clock is sourcing but gating this clock
	 * prevents the system from booting. So, let's mark it as critical.
	 */
	{ BM1880_CLK_SDMA_AXI, "clk_sdma_axi", "clk_axi5",
	  BM1880_CLK_ENABLE0, 23, CLK_IS_CRITICAL },
	{ BM1880_CLK_APB_I2C, "clk_apb_i2c", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 25, 0 },
	{ BM1880_CLK_APB_WDT, "clk_apb_wdt", "clk_mux_axi6",
	  BM1880_CLK_ENABLE0, 26, 0 },
	{ BM1880_CLK_APB_JPEG, "clk_apb_jpeg", "clk_axi6",
	  BM1880_CLK_ENABLE0, 27, 0 },
	{ BM1880_CLK_AXI5_NF, "clk_axi5_nf", "clk_axi5",
	  BM1880_CLK_ENABLE0, 29, 0 },
	{ BM1880_CLK_APB_NF, "clk_apb_nf", "clk_axi6",
	  BM1880_CLK_ENABLE0, 30, 0 },
	{ BM1880_CLK_APB_PWM, "clk_apb_pwm", "clk_mux_axi6",
	  BM1880_CLK_ENABLE1, 0, 0 },
	{ BM1880_CLK_RV, "clk_rv", "clk_mux_rv",
	  BM1880_CLK_ENABLE1, 1, 0 },
	{ BM1880_CLK_APB_SPI, "clk_apb_spi", "clk_mux_axi6",
	  BM1880_CLK_ENABLE1, 2, 0 },
	{ BM1880_CLK_UART_500M, "clk_uart_500m", "clk_div_uart_500m",
	  BM1880_CLK_ENABLE1, 4, 0 },
	{ BM1880_CLK_APB_UART, "clk_apb_uart", "clk_axi6",
	  BM1880_CLK_ENABLE1, 5, 0 },
	{ BM1880_CLK_APB_I2S, "clk_apb_i2s", "clk_axi6",
	  BM1880_CLK_ENABLE1, 6, 0 },
	{ BM1880_CLK_AXI4_USB, "clk_axi4_usb", "clk_axi4",
	  BM1880_CLK_ENABLE1, 7, 0 },
	{ BM1880_CLK_APB_USB, "clk_apb_usb", "clk_axi6",
	  BM1880_CLK_ENABLE1, 8, 0 },
	{ BM1880_CLK_12M_USB, "clk_12m_usb", "clk_div_12m_usb",
	  BM1880_CLK_ENABLE1, 11, 0 },
	{ BM1880_CLK_APB_VIDEO, "clk_apb_video", "clk_axi6",
	  BM1880_CLK_ENABLE1, 12, 0 },
	{ BM1880_CLK_APB_VPP, "clk_apb_vpp", "clk_axi6",
	  BM1880_CLK_ENABLE1, 15, 0 },
	{ BM1880_CLK_AXI6, "clk_axi6", "clk_mux_axi6",
	  BM1880_CLK_ENABLE1, 21, 0 },
};

static const char * const clk_a53_parents[] = { "clk_spll", "clk_mpll" };
static const char * const clk_rv_parents[] = { "clk_div_1_rv", "clk_div_0_rv" };
static const char * const clk_axi1_parents[] = { "clk_div_1_axi1", "clk_div_0_axi1" };
static const char * const clk_axi6_parents[] = { "clk_div_1_axi6", "clk_div_0_axi6" };

static const struct bm1880_mux_clock bm1880_mux_clks[] = {
	{ BM1880_CLK_MUX_RV, "clk_mux_rv", clk_rv_parents, 2,
	  BM1880_CLK_SELECT, 1, 0 },
	{ BM1880_CLK_MUX_AXI6, "clk_mux_axi6", clk_axi6_parents, 2,
	  BM1880_CLK_SELECT, 3, 0 },
};

static const struct clk_div_table bm1880_div_table_0[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
	{ 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
	{ 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
	{ 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },
	{ 20, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 },
	{ 24, 25 }, { 25, 26 }, { 26, 27 }, { 27, 28 },
	{ 28, 29 }, { 29, 30 }, { 30, 31 }, { 31, 32 },
	{ 0, 0 }
};

static const struct clk_div_table bm1880_div_table_1[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
	{ 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
	{ 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
	{ 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },
	{ 20, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 },
	{ 24, 25 }, { 25, 26 }, { 26, 27 }, { 27, 28 },
	{ 28, 29 }, { 29, 30 }, { 30, 31 }, { 31, 32 },
	{ 127, 128 }, { 0, 0 }
};

static const struct clk_div_table bm1880_div_table_2[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
	{ 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
	{ 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
	{ 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },
	{ 20, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 },
	{ 24, 25 }, { 25, 26 }, { 26, 27 }, { 27, 28 },
	{ 28, 29 }, { 29, 30 }, { 30, 31 }, { 31, 32 },
	{ 127, 128 }, { 255, 256 }, { 0, 0 }
};

static const struct clk_div_table bm1880_div_table_3[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
	{ 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
	{ 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
	{ 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },
	{ 20, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 },
	{ 24, 25 }, { 25, 26 }, { 26, 27 }, { 27, 28 },
	{ 28, 29 }, { 29, 30 }, { 30, 31 }, { 31, 32 },
	{ 127, 128 }, { 255, 256 }, { 511, 512 }, { 0, 0 }
};

static const struct clk_div_table bm1880_div_table_4[] = {
	{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 },
	{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 8 },
	{ 8, 9 }, { 9, 10 }, { 10, 11 }, { 11, 12 },
	{ 12, 13 }, { 13, 14 }, { 14, 15 }, { 15, 16 },
	{ 16, 17 }, { 17, 18 }, { 18, 19 }, { 19, 20 },
	{ 20, 21 }, { 21, 22 }, { 22, 23 }, { 23, 24 },
	{ 24, 25 }, { 25, 26 }, { 26, 27 }, { 27, 28 },
	{ 28, 29 }, { 29, 30 }, { 30, 31 }, { 31, 32 },
	{ 127, 128 }, { 255, 256 }, { 511, 512 }, { 65535, 65536 },
	{ 0, 0 }
};

/*
 * Clocks marked as CRITICAL are needed for the proper functioning
 * of the SoC.
 */
static struct bm1880_div_hw_clock bm1880_div_clks[] = {
	CLK_DIV(BM1880_CLK_DIV_0_RV, "clk_div_0_rv", &bm1880_pll_clks[1].hw,
		BM1880_CLK_DIV12, 16, 5, 1, bm1880_div_table_0, 0),
	CLK_DIV(BM1880_CLK_DIV_1_RV, "clk_div_1_rv", &bm1880_pll_clks[2].hw,
		BM1880_CLK_DIV13, 16, 5, 1, bm1880_div_table_0, 0),
	CLK_DIV(BM1880_CLK_DIV_UART_500M, "clk_div_uart_500m", &bm1880_pll_clks[2].hw,
		BM1880_CLK_DIV15, 16, 7, 3, bm1880_div_table_1, 0),
	CLK_DIV(BM1880_CLK_DIV_0_AXI1, "clk_div_0_axi1", &bm1880_pll_clks[0].hw,
		BM1880_CLK_DIV21, 16, 5, 2, bm1880_div_table_0,
		0),
	CLK_DIV(BM1880_CLK_DIV_1_AXI1, "clk_div_1_axi1", &bm1880_pll_clks[2].hw,
		BM1880_CLK_DIV22, 16, 5, 3, bm1880_div_table_0,
		0),
	CLK_DIV(BM1880_CLK_DIV_0_AXI6, "clk_div_0_axi6", &bm1880_pll_clks[2].hw,
		BM1880_CLK_DIV27, 16, 5, 15, bm1880_div_table_0,
		0),
	CLK_DIV(BM1880_CLK_DIV_1_AXI6, "clk_div_1_axi6", &bm1880_pll_clks[0].hw,
		BM1880_CLK_DIV28, 16, 5, 11, bm1880_div_table_0,
		0),
	CLK_DIV(BM1880_CLK_DIV_12M_USB, "clk_div_12m_usb", &bm1880_pll_clks[2].hw,
		BM1880_CLK_DIV18, 16, 7, 125, bm1880_div_table_1, 0),
};

/*
 * Clocks marked as CRITICAL are all needed for the proper functioning
 * of the SoC.
 */
static struct bm1880_composite_clock bm1880_composite_clks[] = {
	/*
	 * Since clk_a53 and clk_50m_a53 clocks are sourcing the CPU core,
	 * let's mark them as critical to avoid gating.
	 */
	GATE_MUX(BM1880_CLK_A53, "clk_a53", clk_a53_parents,
		 BM1880_CLK_ENABLE0, 0, BM1880_CLK_SELECT, 0,
		 CLK_IS_CRITICAL),
	GATE_DIV(BM1880_CLK_50M_A53, "clk_50m_a53", "clk_fpll",
		 BM1880_CLK_ENABLE0, 1, BM1880_CLK_DIV0, 16, 5, 30,
		 bm1880_div_table_0, CLK_IS_CRITICAL),
	GATE_DIV(BM1880_CLK_EFUSE, "clk_efuse", "clk_fpll",
		 BM1880_CLK_ENABLE0, 5, BM1880_CLK_DIV1, 16, 7, 60,
		 bm1880_div_table_1, 0),
	GATE_DIV(BM1880_CLK_EMMC, "clk_emmc", "clk_fpll",
		 BM1880_CLK_ENABLE0, 8, BM1880_CLK_DIV2, 16, 5, 15,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_100K_EMMC, "clk_100k_emmc", "clk_div_12m_usb",
		 BM1880_CLK_ENABLE0, 9, BM1880_CLK_DIV3, 16, 8, 120,
		 bm1880_div_table_2, 0),
	GATE_DIV(BM1880_CLK_SD, "clk_sd", "clk_fpll",
		 BM1880_CLK_ENABLE0, 11, BM1880_CLK_DIV4, 16, 5, 15,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_100K_SD, "clk_100k_sd", "clk_div_12m_usb",
		 BM1880_CLK_ENABLE0, 12, BM1880_CLK_DIV5, 16, 8, 120,
		 bm1880_div_table_2, 0),
	GATE_DIV(BM1880_CLK_500M_ETH0, "clk_500m_eth0", "clk_fpll",
		 BM1880_CLK_ENABLE0, 13, BM1880_CLK_DIV6, 16, 5, 3,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_500M_ETH1, "clk_500m_eth1", "clk_fpll",
		 BM1880_CLK_ENABLE0, 15, BM1880_CLK_DIV7, 16, 5, 3,
		 bm1880_div_table_0, 0),
	/* Don't gate GPIO clocks as it is not owned by the GPIO driver */
	GATE_DIV(BM1880_CLK_GPIO_DB, "clk_gpio_db", "clk_div_12m_usb",
		 BM1880_CLK_ENABLE0, 20, BM1880_CLK_DIV8, 16, 16, 120,
		 bm1880_div_table_4, CLK_IGNORE_UNUSED),
	GATE_DIV(BM1880_CLK_SDMA_AUD, "clk_sdma_aud", "clk_fpll",
		 BM1880_CLK_ENABLE0, 24, BM1880_CLK_DIV9, 16, 7, 61,
		 bm1880_div_table_1, 0),
	GATE_DIV(BM1880_CLK_JPEG_AXI, "clk_jpeg_axi", "clk_fpll",
		 BM1880_CLK_ENABLE0, 28, BM1880_CLK_DIV10, 16, 5, 4,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_NF, "clk_nf", "clk_fpll",
		 BM1880_CLK_ENABLE0, 31, BM1880_CLK_DIV11, 16, 5, 30,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_TPU_AXI, "clk_tpu_axi", "clk_spll",
		 BM1880_CLK_ENABLE1, 3, BM1880_CLK_DIV14, 16, 5, 1,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_125M_USB, "clk_125m_usb", "clk_fpll",
		 BM1880_CLK_ENABLE1, 9, BM1880_CLK_DIV16, 16, 5, 12,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_33K_USB, "clk_33k_usb", "clk_div_12m_usb",
		 BM1880_CLK_ENABLE1, 10, BM1880_CLK_DIV17, 16, 9, 363,
		 bm1880_div_table_3, 0),
	GATE_DIV(BM1880_CLK_VIDEO_AXI, "clk_video_axi", "clk_fpll",
		 BM1880_CLK_ENABLE1, 13, BM1880_CLK_DIV19, 16, 5, 4,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_VPP_AXI, "clk_vpp_axi", "clk_fpll",
		 BM1880_CLK_ENABLE1, 14, BM1880_CLK_DIV20, 16, 5, 4,
		 bm1880_div_table_0, 0),
	GATE_MUX(BM1880_CLK_AXI1, "clk_axi1", clk_axi1_parents,
		 BM1880_CLK_ENABLE1, 15, BM1880_CLK_SELECT, 2, 0),
	GATE_DIV(BM1880_CLK_AXI2, "clk_axi2", "clk_fpll",
		 BM1880_CLK_ENABLE1, 17, BM1880_CLK_DIV23, 16, 5, 3,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_AXI3, "clk_axi3", "clk_mux_rv",
		 BM1880_CLK_ENABLE1, 18, BM1880_CLK_DIV24, 16, 5, 2,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_AXI4, "clk_axi4", "clk_fpll",
		 BM1880_CLK_ENABLE1, 19, BM1880_CLK_DIV25, 16, 5, 6,
		 bm1880_div_table_0, 0),
	GATE_DIV(BM1880_CLK_AXI5, "clk_axi5", "clk_fpll",
		 BM1880_CLK_ENABLE1, 20, BM1880_CLK_DIV26, 16, 5, 15,
		 bm1880_div_table_0, 0),
};

static unsigned long bm1880_pll_rate_calc(u32 regval, unsigned long parent_rate)
{
	u64 numerator;
	u32 fbdiv, refdiv;
	u32 postdiv1, postdiv2, denominator;

	fbdiv = (regval >> 16) & 0xfff;
	refdiv = regval & 0x1f;
	postdiv1 = (regval >> 8) & 0x7;
	postdiv2 = (regval >> 12) & 0x7;

	numerator = parent_rate * fbdiv;
	denominator = refdiv * postdiv1 * postdiv2;
	do_div(numerator, denominator);

	return (unsigned long)numerator;
}

static unsigned long bm1880_pll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate)
{
	struct bm1880_pll_hw_clock *pll_hw = to_bm1880_pll_clk(hw);
	unsigned long rate;
	u32 regval;

	regval = readl(pll_hw->base + pll_hw->pll.reg);
	rate = bm1880_pll_rate_calc(regval, parent_rate);

	return rate;
}

static const struct clk_ops bm1880_pll_ops = {
	.recalc_rate	= bm1880_pll_recalc_rate,
};

static struct clk_hw *bm1880_clk_register_pll(struct bm1880_pll_hw_clock *pll_clk,
					      void __iomem *sys_base)
{
	struct clk_hw *hw;
	int err;

	pll_clk->base = sys_base;
	hw = &pll_clk->hw;

	err = clk_hw_register(NULL, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

static int bm1880_clk_register_plls(struct bm1880_pll_hw_clock *clks,
				    int num_clks,
				    struct bm1880_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *pll_base = data->pll_base;
	int i;

	for (i = 0; i < num_clks; i++) {
		struct bm1880_pll_hw_clock *bm1880_clk = &clks[i];

		hw = bm1880_clk_register_pll(bm1880_clk, pll_base);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, bm1880_clk->pll.name);
			goto err_clk;
		}

		data->hw_data.hws[clks[i].pll.id] = hw;
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister(data->hw_data.hws[clks[i].pll.id]);

	return PTR_ERR(hw);
}

static int bm1880_clk_register_mux(const struct bm1880_mux_clock *clks,
				   int num_clks,
				   struct bm1880_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *sys_base = data->sys_base;
	int i;

	for (i = 0; i < num_clks; i++) {
		hw = clk_hw_register_mux(NULL, clks[i].name,
					 clks[i].parents,
					 clks[i].num_parents,
					 clks[i].flags,
					 sys_base + clks[i].reg,
					 clks[i].shift, 1, 0,
					 &bm1880_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			goto err_clk;
		}

		data->hw_data.hws[clks[i].id] = hw;
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister_mux(data->hw_data.hws[clks[i].id]);

	return PTR_ERR(hw);
}

static unsigned long bm1880_clk_div_recalc_rate(struct clk_hw *hw,
						unsigned long parent_rate)
{
	struct bm1880_div_hw_clock *div_hw = to_bm1880_div_clk(hw);
	struct bm1880_div_clock *div = &div_hw->div;
	void __iomem *reg_addr = div_hw->base + div->reg;
	unsigned int val;
	unsigned long rate;

	if (!(readl(reg_addr) & BIT(3))) {
		val = div->initval;
	} else {
		val = readl(reg_addr) >> div->shift;
		val &= clk_div_mask(div->width);
	}

	rate = divider_recalc_rate(hw, parent_rate, val, div->table,
				   div->flags, div->width);

	return rate;
}

static int bm1880_clk_div_determine_rate(struct clk_hw *hw,
					 struct clk_rate_request *req)
{
	struct bm1880_div_hw_clock *div_hw = to_bm1880_div_clk(hw);
	struct bm1880_div_clock *div = &div_hw->div;
	void __iomem *reg_addr = div_hw->base + div->reg;

	if (div->flags & CLK_DIVIDER_READ_ONLY) {
		u32 val;

		val = readl(reg_addr) >> div->shift;
		val &= clk_div_mask(div->width);

		req->rate = divider_ro_round_rate(hw, req->rate,
						  &req->best_parent_rate,
						  div->table,
						  div->width, div->flags, val);

		return 0;
	}

	req->rate = divider_round_rate(hw, req->rate, &req->best_parent_rate,
				       div->table, div->width, div->flags);

	return 0;
}

static int bm1880_clk_div_set_rate(struct clk_hw *hw, unsigned long rate,
				   unsigned long parent_rate)
{
	struct bm1880_div_hw_clock *div_hw = to_bm1880_div_clk(hw);
	struct bm1880_div_clock *div = &div_hw->div;
	void __iomem *reg_addr = div_hw->base + div->reg;
	unsigned long flags = 0;
	int value;
	u32 val;

	value = divider_get_val(rate, parent_rate, div->table,
				div->width, div_hw->div.flags);
	if (value < 0)
		return value;

	if (div_hw->lock)
		spin_lock_irqsave(div_hw->lock, flags);
	else
		__acquire(div_hw->lock);

	val = readl(reg_addr);
	val &= ~(clk_div_mask(div->width) << div_hw->div.shift);
	val |= (u32)value << div->shift;
	writel(val, reg_addr);

	if (div_hw->lock)
		spin_unlock_irqrestore(div_hw->lock, flags);
	else
		__release(div_hw->lock);

	return 0;
}

static const struct clk_ops bm1880_clk_div_ops = {
	.recalc_rate = bm1880_clk_div_recalc_rate,
	.determine_rate = bm1880_clk_div_determine_rate,
	.set_rate = bm1880_clk_div_set_rate,
};

static struct clk_hw *bm1880_clk_register_div(struct bm1880_div_hw_clock *div_clk,
					      void __iomem *sys_base)
{
	struct clk_hw *hw;
	int err;

	div_clk->div.flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO;
	div_clk->base = sys_base;
	div_clk->lock = &bm1880_clk_lock;

	hw = &div_clk->hw;
	err = clk_hw_register(NULL, hw);
	if (err)
		return ERR_PTR(err);

	return hw;
}

static int bm1880_clk_register_divs(struct bm1880_div_hw_clock *clks,
				    int num_clks,
				    struct bm1880_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *sys_base = data->sys_base;
	unsigned int i, id;

	for (i = 0; i < num_clks; i++) {
		struct bm1880_div_hw_clock *bm1880_clk = &clks[i];

		hw = bm1880_clk_register_div(bm1880_clk, sys_base);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, bm1880_clk->div.name);
			goto err_clk;
		}

		id = clks[i].div.id;
		data->hw_data.hws[id] = hw;
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister(data->hw_data.hws[clks[i].div.id]);

	return PTR_ERR(hw);
}

static int bm1880_clk_register_gate(const struct bm1880_gate_clock *clks,
				    int num_clks,
				    struct bm1880_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *sys_base = data->sys_base;
	int i;

	for (i = 0; i < num_clks; i++) {
		hw = clk_hw_register_gate(NULL, clks[i].name,
					  clks[i].parent,
					  clks[i].flags,
					  sys_base + clks[i].gate_reg,
					  clks[i].gate_shift, 0,
					  &bm1880_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			goto err_clk;
		}

		data->hw_data.hws[clks[i].id] = hw;
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister_gate(data->hw_data.hws[clks[i].id]);

	return PTR_ERR(hw);
}

static struct clk_hw *bm1880_clk_register_composite(struct bm1880_composite_clock *clks,
						    void __iomem *sys_base)
{
	struct clk_hw *hw;
	struct clk_mux *mux = NULL;
	struct clk_gate *gate = NULL;
	struct bm1880_div_hw_clock *div_hws = NULL;
	struct clk_hw *mux_hw = NULL, *gate_hw = NULL, *div_hw = NULL;
	const struct clk_ops *mux_ops = NULL, *gate_ops = NULL, *div_ops = NULL;
	const char * const *parent_names;
	const char *parent;
	int num_parents;
	int ret;

	if (clks->mux_shift >= 0) {
		mux = kzalloc(sizeof(*mux), GFP_KERNEL);
		if (!mux)
			return ERR_PTR(-ENOMEM);

		mux->reg = sys_base + clks->mux_reg;
		mux->mask = 1;
		mux->shift = clks->mux_shift;
		mux_hw = &mux->hw;
		mux_ops = &clk_mux_ops;
		mux->lock = &bm1880_clk_lock;

		parent_names = clks->parents;
		num_parents = clks->num_parents;
	} else {
		parent = clks->parent;
		parent_names = &parent;
		num_parents = 1;
	}

	if (clks->gate_shift >= 0) {
		gate = kzalloc(sizeof(*gate), GFP_KERNEL);
		if (!gate) {
			ret = -ENOMEM;
			goto err_out;
		}

		gate->reg = sys_base + clks->gate_reg;
		gate->bit_idx = clks->gate_shift;
		gate->lock = &bm1880_clk_lock;

		gate_hw = &gate->hw;
		gate_ops = &clk_gate_ops;
	}

	if (clks->div_shift >= 0) {
		div_hws = kzalloc(sizeof(*div_hws), GFP_KERNEL);
		if (!div_hws) {
			ret = -ENOMEM;
			goto err_out;
		}

		div_hws->base = sys_base;
		div_hws->div.reg = clks->div_reg;
		div_hws->div.shift = clks->div_shift;
		div_hws->div.width = clks->div_width;
		div_hws->div.table = clks->table;
		div_hws->div.initval = clks->div_initval;
		div_hws->lock = &bm1880_clk_lock;
		div_hws->div.flags = CLK_DIVIDER_ONE_BASED |
				     CLK_DIVIDER_ALLOW_ZERO;

		div_hw = &div_hws->hw;
		div_ops = &bm1880_clk_div_ops;
	}

	hw = clk_hw_register_composite(NULL, clks->name, parent_names,
				       num_parents, mux_hw, mux_ops, div_hw,
				       div_ops, gate_hw, gate_ops,
				       clks->flags);

	if (IS_ERR(hw)) {
		ret = PTR_ERR(hw);
		goto err_out;
	}

	return hw;

err_out:
	kfree(div_hws);
	kfree(gate);
	kfree(mux);

	return ERR_PTR(ret);
}

static int bm1880_clk_register_composites(struct bm1880_composite_clock *clks,
					  int num_clks,
					  struct bm1880_clock_data *data)
{
	struct clk_hw *hw;
	void __iomem *sys_base = data->sys_base;
	int i;

	for (i = 0; i < num_clks; i++) {
		struct bm1880_composite_clock *bm1880_clk = &clks[i];

		hw = bm1880_clk_register_composite(bm1880_clk, sys_base);
		if (IS_ERR(hw)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, bm1880_clk->name);
			goto err_clk;
		}

		data->hw_data.hws[clks[i].id] = hw;
	}

	return 0;

err_clk:
	while (i--)
		clk_hw_unregister_composite(data->hw_data.hws[clks[i].id]);

	return PTR_ERR(hw);
}

static int bm1880_clk_probe(struct platform_device *pdev)
{
	struct bm1880_clock_data *clk_data;
	void __iomem *pll_base, *sys_base;
	struct device *dev = &pdev->dev;
	int num_clks, i;

	pll_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pll_base))
		return PTR_ERR(pll_base);

	sys_base = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(sys_base))
		return PTR_ERR(sys_base);

	num_clks = ARRAY_SIZE(bm1880_pll_clks) +
		   ARRAY_SIZE(bm1880_div_clks) +
		   ARRAY_SIZE(bm1880_mux_clks) +
		   ARRAY_SIZE(bm1880_composite_clks) +
		   ARRAY_SIZE(bm1880_gate_clks);

	clk_data = devm_kzalloc(dev, struct_size(clk_data, hw_data.hws,
						 num_clks), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->pll_base = pll_base;
	clk_data->sys_base = sys_base;

	for (i = 0; i < num_clks; i++)
		clk_data->hw_data.hws[i] = ERR_PTR(-ENOENT);

	clk_data->hw_data.num = num_clks;

	bm1880_clk_register_plls(bm1880_pll_clks,
				 ARRAY_SIZE(bm1880_pll_clks),
				 clk_data);

	bm1880_clk_register_divs(bm1880_div_clks,
				 ARRAY_SIZE(bm1880_div_clks),
				 clk_data);

	bm1880_clk_register_mux(bm1880_mux_clks,
				ARRAY_SIZE(bm1880_mux_clks),
				clk_data);

	bm1880_clk_register_composites(bm1880_composite_clks,
				       ARRAY_SIZE(bm1880_composite_clks),
				       clk_data);

	bm1880_clk_register_gate(bm1880_gate_clks,
				 ARRAY_SIZE(bm1880_gate_clks),
				 clk_data);

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get,
				      &clk_data->hw_data);
}

static const struct of_device_id bm1880_of_match[] = {
	{ .compatible = "bitmain,bm1880-clk", },
	{}
};
MODULE_DEVICE_TABLE(of, bm1880_of_match);

static struct platform_driver bm1880_clk_driver = {
	.driver = {
		.name = "bm1880-clk",
		.of_match_table = bm1880_of_match,
	},
	.probe = bm1880_clk_probe,
};
module_platform_driver(bm1880_clk_driver);

MODULE_AUTHOR("Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>");
MODULE_DESCRIPTION("Clock driver for Bitmain BM1880 SoC");
