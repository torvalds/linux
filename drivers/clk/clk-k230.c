// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kendryte Canaan K230 Clock Drivers
 *
 * Author: Xukai Wang <kingxukai@zohomail.com>
 * Author: Troy Mitchell <troymitchell988@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/rational.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/canaan,k230-clk.h>

/* PLL control register bits. */
#define K230_PLL_BYPASS_ENABLE			BIT(19)
#define K230_PLL_GATE_ENABLE			BIT(2)
#define K230_PLL_GATE_WRITE_ENABLE		BIT(18)
#define K230_PLL_OD_MASK			GENMASK(27, 24)
#define K230_PLL_R_MASK				GENMASK(21, 16)
#define K230_PLL_F_MASK				GENMASK(12, 0)
#define K230_PLL_DIV_REG_OFFSET			0x00
#define K230_PLL_BYPASS_REG_OFFSET		0x04
#define K230_PLL_GATE_REG_OFFSET		0x08
#define K230_PLL_LOCK_REG_OFFSET		0x0C

/* PLL lock register */
#define K230_PLL_LOCK_STATUS_MASK		BIT(0)
#define K230_PLL_LOCK_TIME_DELAY		400
#define K230_PLL_LOCK_TIMEOUT			0

/* K230 CLK registers offset */
#define K230_CLK_AUDIO_CLKDIV_OFFSET		0x34
#define K230_CLK_PDM_CLKDIV_OFFSET		0x40
#define K230_CLK_CODEC_ADC_MCLKDIV_OFFSET	0x38
#define K230_CLK_CODEC_DAC_MCLKDIV_OFFSET	0x3c

#define K230_PLLX_DIV_ADDR(base, idx)						\
	(K230_PLL_DIV_REG_OFFSET + (base) + (idx) * 0x10)

#define K230_PLLX_BYPASS_ADDR(base, idx)					\
	(K230_PLL_BYPASS_REG_OFFSET + (base) + (idx) * 0x10)

#define K230_PLLX_GATE_ADDR(base, idx)						\
	(K230_PLL_GATE_REG_OFFSET + (base) + (idx) * 0x10)

#define K230_PLLX_LOCK_ADDR(base, idx)						\
	(K230_PLL_LOCK_REG_OFFSET + (base) + (idx) * 0x10)

#define K230_CLK_RATE_FORMAT_PNAME(_var, _id,					\
				   _mul_min, _mul_max, _mul_shift, _mul_mask,	\
				   _div_min, _div_max, _div_shift, _div_mask,	\
				   _reg, _bit, _method, _reg2,			\
				   _read_only, _flags,				\
				   _pname)					\
	static struct k230_clk_rate _var = {					\
		.div_reg_off = _reg,						\
		.mul_reg_off = _reg2,						\
		.id = _id,							\
		.clk = {							\
			.write_enable_bit = _bit,				\
			.mul_min = _mul_min,					\
			.mul_max = _mul_max,					\
			.mul_shift = _mul_shift,				\
			.mul_mask = _mul_mask,					\
			.div_min = _div_min,					\
			.div_max = _div_max,					\
			.div_shift = _div_shift,				\
			.div_mask = _div_mask,					\
			.read_only = _read_only,				\
			.hw.init = CLK_HW_INIT_FW_NAME(#_var,			\
				   _pname, &k230_clk_ops_##_method,		\
				   _flags),					\
		},								\
	}

#define K230_CLK_RATE_FORMAT(_var, _id,						\
			     _mul_min, _mul_max, _mul_shift, _mul_mask,		\
			     _div_min, _div_max, _div_shift, _div_mask,		\
			     _reg, _bit, _method, _reg2,			\
			     _read_only, _flags,				\
			     _phw)						\
	static struct k230_clk_rate _var = {					\
		.div_reg_off = _reg,						\
		.mul_reg_off = _reg2,						\
		.id = _id,							\
		.clk = {							\
			.write_enable_bit = _bit,				\
			.mul_min = _mul_min,					\
			.mul_max = _mul_max,					\
			.mul_shift = _mul_shift,				\
			.mul_mask = _mul_mask,					\
			.div_min = _div_min,					\
			.div_max = _div_max,					\
			.div_shift = _div_shift,				\
			.div_mask = _div_mask,					\
			.read_only = _read_only,				\
			.hw.init = CLK_HW_INIT_HW(#_var,			\
				   _phw, &k230_clk_ops_##_method,		\
				   _flags),					\
		},								\
	}

#define K230_CLK_GATE_FORMAT_PNAME(_var, _id,					\
				   _reg, _bit, _flags, _gate_flags,		\
				   _pname)					\
	static struct k230_clk_gate _var = {					\
		.reg_off = _reg,						\
		.id = _id,							\
		.clk = {							\
			.bit_idx = _bit,					\
			.flags = _gate_flags,					\
			.hw.init = CLK_HW_INIT_FW_NAME(#_var,			\
				   _pname, &clk_gate_ops, _flags),		\
		},								\
	}

#define K230_CLK_GATE_FORMAT(_var, _id,						\
			     _reg, _bit, _flags, _gate_flags,			\
			     _phw)						\
	static struct k230_clk_gate _var = {					\
		.reg_off = _reg,						\
		.id = _id,							\
		.clk = {							\
			.bit_idx = _bit,					\
			.flags = _gate_flags,					\
			.hw.init = CLK_HW_INIT_HW(#_var,			\
				   _phw, &clk_gate_ops, _flags),		\
		},								\
	}

#define K230_CLK_MUX_FORMAT(_var, _id,						\
			    _reg, _shift, _mask, _flags, _mux_flags, _pdata)	\
	static struct k230_clk_mux _var = {					\
		.reg_off = _reg,						\
		.id = _id,							\
		.clk = {							\
			.flags = _mux_flags,					\
			.shift = _shift,					\
			.mask = _mask,						\
			.hw.init = CLK_HW_INIT_PARENTS_DATA(#_var,		\
				   _pdata, &clk_mux_ops, _flags),		\
		},								\
	}

#define K230_CLK_FIXED_FACTOR_FORMAT(_var,					\
				     _mul, _div, _flags,			\
				     _phw)					\
	static struct clk_fixed_factor _var = {					\
		.mult = _mul,							\
		.div = _div,							\
		.hw.init = CLK_HW_INIT_HW(#_var,				\
			   _phw, &clk_fixed_factor_ops, _flags),		\
	}

#define K230_CLK_PLL_FORMAT(_var, _id, _flags, _pname)				\
	static struct k230_pll _var = {						\
		.hw.init = CLK_HW_INIT_FW_NAME(#_var,				\
			   _pname, &k230_pll_ops, _flags),			\
		.id = _id,							\
	}

struct k230_pll {
	struct clk_hw	hw;
	void __iomem	*reg;
	/* ensures mutual exclusion for concurrent register access. */
	spinlock_t	*lock;
	int id;
};

#define hw_to_k230_pll(_hw) container_of(_hw, struct k230_pll, hw)

struct k230_clk_rate_self {
	struct clk_hw	hw;
	void __iomem	*reg;
	bool		read_only;
	u32		write_enable_bit;
	u32		mul_min;
	u32		mul_max;
	u32		mul_shift;
	u32		mul_mask;
	u32		div_min;
	u32		div_max;
	u32		div_shift;
	u32		div_mask;
	/* ensures mutual exclusion for concurrent register access. */
	spinlock_t	*lock;
};

#define hw_to_k230_clk_rate_self(_hw)	container_of(_hw,			\
					struct k230_clk_rate_self, hw)

struct k230_clk_rate {
	u32				mul_reg_off;
	u32				div_reg_off;
	struct k230_clk_rate_self	clk;
	int				id;
};

static inline struct k230_clk_rate *hw_to_k230_clk_rate(struct clk_hw *hw)
{
	return container_of(hw_to_k230_clk_rate_self(hw), struct k230_clk_rate,
			    clk);
}

struct k230_clk_gate {
	u32			reg_off;
	struct clk_gate		clk;
	int			id;
};

struct k230_clk_mux {
	u32			reg_off;
	struct clk_mux		clk;
	int			id;
};

static int k230_pll_prepare(struct clk_hw *hw);
static int k230_pll_enable(struct clk_hw *hw);
static void k230_pll_disable(struct clk_hw *hw);
static int k230_pll_is_enabled(struct clk_hw *hw);
static unsigned long k230_pll_get_rate(struct clk_hw *hw, unsigned long parent_rate);
static int k230_clk_set_rate_mul(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate);
static int k230_clk_determine_rate_mul(struct clk_hw *hw, struct clk_rate_request *req);
static unsigned long k230_clk_get_rate_mul(struct clk_hw *hw,
					   unsigned long parent_rate);
static int k230_clk_set_rate_div(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate);
static int k230_clk_determine_rate_div(struct clk_hw *hw, struct clk_rate_request *req);
static unsigned long k230_clk_get_rate_div(struct clk_hw *hw,
					   unsigned long parent_rate);
static int k230_clk_set_rate_mul_div(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate);
static int k230_clk_determine_rate_mul_div(struct clk_hw *hw, struct clk_rate_request *req);
static unsigned long k230_clk_get_rate_mul_div(struct clk_hw *hw,
					       unsigned long parent_rate);

static const struct clk_ops k230_pll_ops = {
	.prepare	= k230_pll_prepare,
	.enable	        = k230_pll_enable,
	.disable	= k230_pll_disable,
	.is_enabled	= k230_pll_is_enabled,
	.recalc_rate	= k230_pll_get_rate,
};

/* clk_ops for clocks whose rate is determined by a configurable multiplier */
static const struct clk_ops k230_clk_ops_mul = {
	.set_rate	= k230_clk_set_rate_mul,
	.determine_rate	= k230_clk_determine_rate_mul,
	.recalc_rate	= k230_clk_get_rate_mul,
};

/* clk_ops for clocks whose rate is determined by a configurable divider */
static const struct clk_ops k230_clk_ops_div = {
	.set_rate	= k230_clk_set_rate_div,
	.determine_rate	= k230_clk_determine_rate_div,
	.recalc_rate	= k230_clk_get_rate_div,
};

/* clk_ops for clocks whose rate is determined by both a multiplier and a divider */
static const struct clk_ops k230_clk_ops_mul_div = {
	.set_rate	= k230_clk_set_rate_mul_div,
	.determine_rate	= k230_clk_determine_rate_mul_div,
	.recalc_rate	= k230_clk_get_rate_mul_div,
};

K230_CLK_PLL_FORMAT(pll0, 0, CLK_IS_CRITICAL, NULL);
K230_CLK_PLL_FORMAT(pll1, 1, CLK_IS_CRITICAL, NULL);
K230_CLK_PLL_FORMAT(pll2, 2, CLK_IS_CRITICAL, NULL);
K230_CLK_PLL_FORMAT(pll3, 3, CLK_IS_CRITICAL, NULL);

static struct k230_pll *k230_plls[] = {
	&pll0,
	&pll1,
	&pll2,
	&pll3,
};

K230_CLK_FIXED_FACTOR_FORMAT(pll0_div2, 1, 2, 0, &pll0.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll0_div3, 1, 3, 0, &pll0.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll0_div4, 1, 4, 0, &pll0.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll0_div16, 1, 16, 0, &pll0.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll1_div2, 1, 2, 0, &pll1.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll1_div3, 1, 3, 0, &pll1.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll1_div4, 1, 4, 0, &pll1.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll2_div2, 1, 2, 0, &pll2.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll2_div3, 1, 3, 0, &pll2.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll2_div4, 1, 4, 0, &pll2.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll3_div2, 1, 2, 0, &pll3.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll3_div3, 1, 3, 0, &pll3.hw);
K230_CLK_FIXED_FACTOR_FORMAT(pll3_div4, 1, 4, 0, &pll3.hw);

static struct clk_fixed_factor *k230_pll_divs[] = {
	&pll0_div2,
	&pll0_div3,
	&pll0_div4,
	&pll0_div16,
	&pll1_div2,
	&pll1_div3,
	&pll1_div4,
	&pll2_div2,
	&pll2_div3,
	&pll2_div4,
	&pll3_div2,
	&pll3_div3,
	&pll3_div4,
};

K230_CLK_GATE_FORMAT(cpu0_src_gate,
		     K230_CPU0_SRC_GATE,
		     0, 0, CLK_IS_CRITICAL, 0,
		     &pll0_div2.hw);

K230_CLK_RATE_FORMAT(cpu0_src_rate,
		     K230_CPU0_SRC_RATE,
		     1, 16, 1, 0xF,
		     16, 16, 0, 0x0,
		     0x0, 31, mul, 0x0,
		     false, 0,
		     &cpu0_src_gate.clk.hw);

K230_CLK_RATE_FORMAT(cpu0_axi_rate,
		     K230_CPU0_AXI_RATE,
		     1, 1, 0, 0,
		     1, 8, 6, 0x7,
		     0x0, 31, div, 0x0,
		     0, 0,
		     &cpu0_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(cpu0_plic_gate,
		     K230_CPU0_PLIC_GATE,
		     0x0, 9, CLK_IS_CRITICAL, 0,
		     &cpu0_src_rate.clk.hw);

K230_CLK_RATE_FORMAT(cpu0_plic_rate,
		     K230_CPU0_PLIC_RATE,
		     1, 1, 0, 0,
		     1, 8, 10, 0x7,
		     0x0, 31, div, 0x0,
		     false, 0,
		     &cpu0_plic_gate.clk.hw);

K230_CLK_GATE_FORMAT(cpu0_noc_ddrcp4_gate,
		     K230_CPU0_NOC_DDRCP4_GATE,
		     0x60, 7, CLK_IS_CRITICAL, 0,
		     &cpu0_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(cpu0_apb_gate,
		     K230_CPU0_APB_GATE,
		     0x0, 13, CLK_IS_CRITICAL, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(cpu0_apb_rate,
		     K230_CPU0_APB_RATE,
		     1, 1, 0, 0,
		     1, 8, 15, 0x7,
		     0x0, 31, div, 0x0,
		     false, 0,
		     &cpu0_apb_gate.clk.hw);

static const struct clk_parent_data k230_cpu1_src_mux_pdata[] = {
	{ .hw = &pll1_div2.hw, },
	{ .hw = &pll3.hw, },
	{ .hw = &pll0.hw, },
};

K230_CLK_MUX_FORMAT(cpu1_src_mux,
		    K230_CPU1_SRC_MUX,
		    0x4, 1, 0x3,
		    0, 0,
		    k230_cpu1_src_mux_pdata);

K230_CLK_GATE_FORMAT(cpu1_src_gate,
		     K230_CPU1_SRC_GATE,
		     0x4, 0, CLK_IS_CRITICAL, 0,
		     &cpu1_src_mux.clk.hw);

K230_CLK_RATE_FORMAT(cpu1_src_rate,
		     K230_CPU1_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x4, 31, div, 0x0,
		     false, 0,
		     &cpu1_src_gate.clk.hw);

K230_CLK_RATE_FORMAT(cpu1_axi_rate,
		     K230_CPU1_AXI_RATE,
		     1, 1, 0, 0,
		     1, 8, 12, 0x7,
		     0x4, 31, div, 0x0,
		     false, 0,
		     &cpu1_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(cpu1_plic_gate,
		     K230_CPU1_PLIC_GATE,
		     0x4, 15, CLK_IS_CRITICAL, 0,
		     &cpu1_src_rate.clk.hw);

K230_CLK_RATE_FORMAT(cpu1_plic_rate,
		     K230_CPU1_PLIC_RATE,
		     1, 1, 0, 0,
		     1, 8, 16, 0x7,
		     0x4, 31, div, 0x0,
		     false, 0,
		     &cpu1_plic_gate.clk.hw);

K230_CLK_GATE_FORMAT(cpu1_apb_gate,
		     K230_CPU1_APB_GATE,
		     0x4, 19, CLK_IS_CRITICAL, 0,
		     &pll0_div4.hw);

K230_CLK_GATE_FORMAT_PNAME(pmu_apb_gate,
			   K230_PMU_APB_GATE,
			   0x10, 0, 0, 0,
			   "osc24m");

K230_CLK_GATE_FORMAT(hs_hclk_high_gate,
		     K230_HS_HCLK_HIGH_GATE,
		     0x18, 1, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_hclk_high_rate,
		     K230_HS_HCLK_HIGH_RATE,
		     1, 1, 0, 0,
		     1, 8, 0, 0x7,
		     0x1C, 31, div, 0x0,
		     false, 0,
		     &hs_hclk_high_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_hclk_gate,
		     K230_HS_HCLK_GATE,
		     0x18, 0, 0, 0,
		     &hs_hclk_high_rate.clk.hw);

K230_CLK_RATE_FORMAT(hs_hclk_rate,
		     K230_HS_HCLK_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x1C, 31, div, 0x0,
		     false, 0,
		     &hs_hclk_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd0_ahb_gate,
		     K230_HS_SD0_AHB_GATE,
		     0x18, 2, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd1_ahb_gate,
		     K230_HS_SD1_AHB_GATE,
		     0x18, 3, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi1_ahb_gate,
		     K230_HS_SSI1_AHB_GATE,
		     0x18, 7, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi2_ahb_gate,
		     K230_HS_SSI2_AHB_GATE,
		     0x18, 8, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_usb0_ahb_gate,
		     K230_HS_USB0_AHB_GATE,
		     0x18, 4, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_usb1_ahb_gate,
		     K230_HS_USB1_AHB_GATE,
		     0x18, 5, 0, 0,
		     &hs_hclk_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi0_axi_gate,
		     K230_HS_SSI0_AXI_GATE,
		     0x18, 27, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_ssi0_axi_rate,
		     K230_HS_SSI0_AXI_RATE,
		     1, 1, 0, 0,
		     1, 8, 9, 0x7,
		     0x20, 31, div, 0x0,
		     false, 0,
		     &hs_ssi0_axi_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi1_gate,
		     K230_HS_SSI1_GATE,
		     0x18, 25, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_ssi1_rate,
		     K230_HS_SSI1_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x20, 31, div, 0x0,
		     false, 0,
		     &hs_ssi1_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi2_gate,
		     K230_HS_SSI2_GATE,
		     0x18, 26, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_ssi2_rate,
		     K230_HS_SSI2_RATE,
		     1, 1, 0, 0,
		     1, 8, 6, 0x7,
		     0x20, 31, div, 0x0,
		     false, 0,
		     &hs_ssi2_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_qspi_axi_src_gate,
		     K230_HS_QSPI_AXI_SRC_GATE,
		     0x18, 28, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_qspi_axi_src_rate,
		     K230_HS_QSPI_AXI_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 12, 0x7,
		     0x20, 31, div, 0x0,
		     false, 0,
		     &hs_qspi_axi_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi1_axi_gate,
		     K230_HS_SSI1_AXI_GATE,
		     0x18, 29, 0, 0,
		     &hs_qspi_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_ssi2_axi_gate,
		     K230_HS_SSI2_AXI_GATE,
		     0x18, 30, 0, 0,
		     &hs_qspi_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd_card_src_gate,
		     K230_HS_SD_CARD_SRC_GATE,
		     0x18, 11, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(hs_sd_card_src_rate,
		     K230_HS_SD_CARD_SRC_RATE,
		     1, 1, 0, 0,
		     2, 8, 12, 0x7,
		     0x1C, 31, div, 0x0,
		     false, 0,
		     &hs_sd_card_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd0_card_gate,
		     K230_HS_SD0_CARD_GATE,
		     0x18, 15, 0, 0,
		     &hs_sd_card_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd1_card_gate,
		     K230_HS_SD1_CARD_GATE,
		     0x18, 19, 0, 0,
		     &hs_sd_card_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd_axi_src_gate,
		     K230_HS_SD_AXI_SRC_GATE,
		     0x18, 9, 0, 0,
		     &pll2_div4.hw);

K230_CLK_RATE_FORMAT(hs_sd_axi_src_rate,
		     K230_HS_SD_AXI_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 6, 0x7,
		     0x1C, 31, div, 0x0,
		     false, 0,
		     &hs_sd_axi_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd0_axi_gate,
		     K230_HS_SD0_AXI_GATE,
		     0x18, 13, 0, 0,
		     &hs_sd_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd1_axi_gate,
		     K230_HS_SD1_AXI_GATE,
		     0x18, 17, 0, 0,
		     &hs_sd_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd0_base_gate,
		     K230_HS_SD0_BASE_GATE,
		     0x18, 14, 0, 0,
		     &hs_sd_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd1_base_gate,
		     K230_HS_SD1_BASE_GATE,
		     0x18, 18, 0, 0,
		     &hs_sd_axi_src_rate.clk.hw);

static const struct clk_parent_data k230_hs_ssi0_mux_pdata[] = {
	{ .hw = &pll0_div2.hw, },
	{ .hw = &pll2_div4.hw, },
};

K230_CLK_MUX_FORMAT(hs_ssi0_mux,
		    K230_HS_SSI0_MUX,
		    0x20, 18, 0x1,
		    0, 0,
		    k230_hs_ssi0_mux_pdata);

K230_CLK_GATE_FORMAT(hs_ssi0_gate,
		     K230_HS_SSI0_GATE,
		     0x18, 24, CLK_IGNORE_UNUSED, 0,
		     &hs_ssi0_mux.clk.hw);

K230_CLK_RATE_FORMAT(hs_usb_ref_50m_rate,
		     K230_HS_USB_REF_50M_RATE,
		     1, 1, 0, 0,
		     1, 8, 15, 0x7,
		     0x20, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT_PNAME(hs_sd_timer_src_gate,
			   K230_HS_SD_TIMER_SRC_GATE,
			   0x18, 12, 0, 0,
			   "osc24m");

K230_CLK_RATE_FORMAT(hs_sd_timer_src_rate,
		     K230_HS_SD_TIMER_SRC_RATE,
		     1, 1, 0, 0,
		     24, 32, 15, 0x1F,
		     0x1C, 31, div, 0x0,
		     false, 0,
		     &hs_sd_timer_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd0_timer_gate,
		     K230_HS_SD0_TIMER_GATE,
		     0x18, 16, 0, 0,
		     &hs_sd_timer_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(hs_sd1_timer_gate,
		     K230_HS_SD1_TIMER_GATE,
		     0x18, 20, 0, 0,
		     &hs_sd_timer_src_rate.clk.hw);

static const struct clk_parent_data k230_hs_usb_ref_mux_pdata[] = {
	{ .fw_name = "osc24m", },
	{ .hw = &hs_usb_ref_50m_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(hs_usb_ref_mux,
		    K230_HS_USB_REF_MUX,
		    0x18, 23, 0x1,
		    0, 0,
		    k230_hs_usb_ref_mux_pdata);

K230_CLK_GATE_FORMAT(hs_usb0_ref_gate,
		     K230_HS_USB0_REF_GATE,
		     0x18, 21, CLK_IGNORE_UNUSED, 0,
		     &hs_usb_ref_mux.clk.hw);

K230_CLK_GATE_FORMAT(hs_usb1_ref_gate,
		     K230_HS_USB1_REF_GATE,
		     0x18, 22, CLK_IGNORE_UNUSED, 0,
		     &hs_usb_ref_mux.clk.hw);

K230_CLK_GATE_FORMAT(ls_apb_src_gate,
		     K230_LS_APB_SRC_GATE,
		     0x24, 0, CLK_IS_CRITICAL, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_apb_src_rate,
		     K230_LS_APB_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 0, 0x7,
		     0x30, 31, div, 0x0,
		     false, 0,
		     &ls_apb_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart0_apb_gate,
		     K230_LS_UART0_APB_GATE,
		     0x24, 1, CLK_IS_CRITICAL, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart1_apb_gate,
		     K230_LS_UART1_APB_GATE,
		     0x24, 2, CLK_IS_CRITICAL, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart2_apb_gate,
		     K230_LS_UART2_APB_GATE,
		     0x24, 3, CLK_IS_CRITICAL, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart3_apb_gate,
		     K230_LS_UART3_APB_GATE,
		     0x24, 4, CLK_IS_CRITICAL, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart4_apb_gate,
		     K230_LS_UART4_APB_GATE,
		     0x24, 5, CLK_IS_CRITICAL, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c0_apb_gate,
		     K230_LS_I2C0_APB_GATE,
		     0x24, 6, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c1_apb_gate,
		     K230_LS_I2C1_APB_GATE,
		     0x24, 7, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c2_apb_gate,
		     K230_LS_I2C2_APB_GATE,
		     0x24, 8, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c3_apb_gate,
		     K230_LS_I2C3_APB_GATE,
		     0x24, 9, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c4_apb_gate,
		     K230_LS_I2C4_APB_GATE,
		     0x24, 10, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_gpio_apb_gate,
		     K230_LS_GPIO_APB_GATE,
		     0x24, 11, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_pwm_apb_gate,
		     K230_LS_PWM_APB_GATE,
		     0x24, 12, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink0_apb_gate,
		     K230_LS_JAMLINK0_APB_GATE,
		     0x28, 4, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink1_apb_gate,
		     K230_LS_JAMLINK1_APB_GATE,
		     0x28, 5, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink2_apb_gate,
		     K230_LS_JAMLINK2_APB_GATE,
		     0x28, 6, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink3_apb_gate,
		     K230_LS_JAMLINK3_APB_GATE,
		     0x28, 7, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_audio_apb_gate,
		     K230_LS_AUDIO_APB_GATE,
		     0x24, 13, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_adc_apb_gate,
		     K230_LS_ADC_APB_GATE,
		     0x24, 15, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_codec_apb_gate,
		     K230_LS_CODEC_APB_GATE,
		     0x24, 14, 0, 0,
		     &ls_apb_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c0_gate,
		     K230_LS_I2C0_GATE,
		     0x24, 21, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_i2c0_rate,
		     K230_LS_I2C0_RATE,
		     1, 1, 0, 0,
		     1, 8, 15, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_i2c0_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c1_gate,
		     K230_LS_I2C1_GATE,
		     0x24, 22, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_i2c1_rate,
		     K230_LS_I2C1_RATE,
		     1, 1, 0, 0,
		     1, 8, 18, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_i2c1_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c2_gate,
		     K230_LS_I2C2_GATE,
		     0x24, 23, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_i2c2_rate,
		     K230_LS_I2C2_RATE,
		     1, 1, 0, 0,
		     1, 8, 21, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_i2c2_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c3_gate,
		     K230_LS_I2C3_GATE,
		     0x24, 24, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_i2c3_rate,
		     K230_LS_I2C3_RATE,
		     1, 1, 0, 0,
		     1, 8, 24, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_i2c3_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_i2c4_gate,
		     K230_LS_I2C4_GATE,
		     0x24, 25, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_i2c4_rate,
		     K230_LS_I2C4_RATE,
		     1, 1, 0, 0,
		     1, 8, 27, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_i2c4_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_codec_adc_gate,
		     K230_LS_CODEC_ADC_GATE,
		     0x24, 29, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_codec_adc_rate,
		     K230_LS_CODEC_ADC_RATE,
		     0x10, 0x1B9, 14, 0x1FFF,
		     0xC35, 0x3D09, 0, 0x3FFF,
		     0x38, 31, mul_div, 0x38,
		     false, 0,
		     &ls_codec_adc_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_codec_dac_gate,
		     K230_LS_CODEC_DAC_GATE,
		     0x24, 30, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_codec_dac_rate,
		     K230_LS_CODEC_DAC_RATE,
		     0x10, 0x1B9, 14, 0x1FFF,
		     0xC35, 0x3D09, 0, 0x3FFF,
		     0x3C, 31, mul_div, 0x3C,
		     false, 0,
		     &ls_codec_dac_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_audio_dev_gate,
		     K230_LS_AUDIO_DEV_GATE,
		     0x24, 28, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_audio_dev_rate,
		     K230_LS_AUDIO_DEV_RATE,
		     0x4, 0x1B9, 16, 0x7FFF,
		     0xC35, 0xF424, 0, 0xFFFF,
		     0x34, 31, mul_div, 0x34,
		     false, 0,
		     &ls_audio_dev_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_pdm_gate,
		     K230_LS_PDM_GATE,
		     0x24, 31, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_pdm_rate,
		     K230_LS_PDM_RATE,
		     0x2, 0x1B9, 0, 0xFFFF,
		     0xC35, 0x1E848, 0, 0x1FFFF,
		     0x40, 0, mul_div, 0x44,
		     false, 0,
		     &ls_pdm_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_adc_gate,
		     K230_LS_ADC_GATE,
		     0x24, 26, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ls_adc_rate,
		     K230_LS_ADC_RATE,
		     1, 1, 0, 0,
		     1, 1024, 3, 0x3FF,
		     0x30, 31, div, 0x0,
		     false, 0,
		     &ls_adc_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart0_gate,
		     K230_LS_UART0_GATE,
		     0x24, 16, CLK_IS_CRITICAL, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(ls_uart0_rate,
		     K230_LS_UART0_RATE,
		     1, 1, 0, 0,
		     1, 8, 0, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_uart0_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart1_gate,
		     K230_LS_UART1_GATE,
		     0x24, 17, CLK_IS_CRITICAL, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(ls_uart1_rate,
		     K230_LS_UART1_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_uart1_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart2_gate,
		     K230_LS_UART2_GATE,
		     0x24, 18, CLK_IS_CRITICAL, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(ls_uart2_rate,
		     K230_LS_UART2_RATE,
		     1, 1, 0, 0,
		     1, 8, 6, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_uart2_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart3_gate,
		     K230_LS_UART3_GATE,
		     0x24, 19, CLK_IS_CRITICAL, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(ls_uart3_rate,
		     K230_LS_UART3_RATE,
		     1, 1, 0, 0,
		     1, 8, 9, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_uart3_gate.clk.hw);

K230_CLK_GATE_FORMAT(ls_uart4_gate,
		     K230_LS_UART4_GATE,
		     0x24, 20, CLK_IS_CRITICAL, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(ls_uart4_rate,
		     K230_LS_UART4_RATE,
		     1, 1, 0, 0,
		     1, 8, 12, 0x7,
		     0x2C, 31, div, 0x0,
		     false, 0,
		     &ls_uart4_gate.clk.hw);

K230_CLK_RATE_FORMAT(ls_jamlinkco_src_rate,
		     K230_LS_JAMLINKCO_SRC_RATE,
		     1, 1, 0, 0,
		     2, 512, 23, 0xFF,
		     0x30, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(ls_jamlink0co_gate,
		     K230_LS_JAMLINK0CO_GATE,
		     0x28, 0, 0, 0,
		     &ls_jamlinkco_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink1co_gate,
		     K230_LS_JAMLINK1CO_GATE,
		     0x28, 1, 0, 0,
		     &ls_jamlinkco_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink2co_gate,
		     K230_LS_JAMLINK2CO_GATE,
		     0x28, 2, 0, 0,
		     &ls_jamlinkco_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(ls_jamlink3co_gate,
		     K230_LS_JAMLINK3CO_GATE,
		     0x28, 3, 0, 0,
		     &ls_jamlinkco_src_rate.clk.hw);

K230_CLK_GATE_FORMAT_PNAME(ls_gpio_debounce_gate,
			   K230_LS_GPIO_DEBOUNCE_GATE,
			   0x24, 27, 0, 0,
			   "osc24m");

K230_CLK_RATE_FORMAT(ls_gpio_debounce_rate,
		     K230_LS_GPIO_DEBOUNCE_RATE,
		     1, 1, 0, 0,
		     1, 1024, 13, 0x3FF,
		     0x30, 31, div, 0x0,
		     false, 0,
		     &ls_gpio_debounce_gate.clk.hw);

K230_CLK_GATE_FORMAT(sysctl_wdt0_apb_gate,
		     K230_SYSCTL_WDT0_APB_GATE,
		     0x50, 1, 0, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(sysctl_wdt1_apb_gate,
		     K230_SYSCTL_WDT1_APB_GATE,
		     0x50, 2, 0, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(sysctl_timer_apb_gate,
		     K230_SYSCTL_TIMER_APB_GATE,
		     0x50, 3, 0, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(sysctl_iomux_apb_gate,
		     K230_SYSCTL_IOMUX_APB_GATE,
		     0x50, 20, 0, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(sysctl_mailbox_apb_gate,
		     K230_SYSCTL_MAILBOX_APB_GATE,
		     0x50, 4, 0, 0,
		     &pll0_div16.hw);

K230_CLK_GATE_FORMAT(sysctl_hdi_gate,
		     K230_SYSCTL_HDI_GATE,
		     0x50, 21, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(sysctl_hdi_rate,
		     K230_SYSCTL_HDI_RATE,
		     1, 1, 0, 0,
		     1, 8, 28, 0x7,
		     0x58, 31, div, 0x0,
		     false, 0,
		     &sysctl_hdi_gate.clk.hw);

K230_CLK_GATE_FORMAT(sysctl_time_stamp_gate,
		     K230_SYSCTL_TIME_STAMP_GATE,
		     0x50, 19, CLK_IS_CRITICAL, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(sysctl_time_stamp_rate,
		     K230_SYSCTL_TIME_STAMP_RATE,
		     1, 1, 0, 0,
		     1, 32, 15, 0x1F,
		     0x58, 31, div, 0x0,
		     false, 0,
		     &sysctl_time_stamp_gate.clk.hw);

K230_CLK_RATE_FORMAT_PNAME(sysctl_temp_sensor_rate,
			   K230_SYSCTL_TEMP_SENSOR_RATE,
			   1, 1, 0, 0,
			   1, 256, 20, 0xFF,
			   0x58, 31, div, 0x0,
			   false, 0,
			   "osc24m");

K230_CLK_GATE_FORMAT_PNAME(sysctl_wdt0_gate,
			   K230_SYSCTL_WDT0_GATE,
			   0x50, 5, 0, 0,
			   "osc24m");

K230_CLK_RATE_FORMAT(sysctl_wdt0_rate,
		     K230_SYSCTL_WDT0_RATE,
		     1, 1, 0, 0,
		     1, 64, 3, 0x3F,
		     0x58, 31, div, 0x0,
		     false, 0,
		     &sysctl_wdt0_gate.clk.hw);

K230_CLK_GATE_FORMAT_PNAME(sysctl_wdt1_gate,
			   K230_SYSCTL_WDT1_GATE,
			   0x50, 6, 0, 0,
			   "osc24m");

K230_CLK_RATE_FORMAT(sysctl_wdt1_rate,
		     K230_SYSCTL_WDT1_RATE,
		     1, 1, 0, 0,
		     1, 64, 9, 0x3F,
		     0x58, 31, div, 0x0,
		     false, 0,
		     &sysctl_wdt1_gate.clk.hw);

K230_CLK_RATE_FORMAT(timer0_src_rate,
		     K230_TIMER0_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 0, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(timer1_src_rate,
		     K230_TIMER1_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(timer2_src_rate,
		     K230_TIMER2_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 6, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(timer3_src_rate,
		     K230_TIMER3_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 9, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(timer4_src_rate,
		     K230_TIMER4_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 12, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

K230_CLK_RATE_FORMAT(timer5_src_rate,
		     K230_TIMER5_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 15, 0x7,
		     0x54, 31, div, 0x0,
		     false, 0,
		     &pll0_div16.hw);

static const struct clk_parent_data k230_timer0_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer0_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer0_mux,
		    K230_TIMER0_MUX,
		    0x50, 7, 0x1,
		    0, 0,
		    k230_timer0_mux_pdata);

K230_CLK_GATE_FORMAT(timer0_gate,
		     K230_TIMER0_GATE,
		     0x50, 13, CLK_IGNORE_UNUSED, 0,
		     &timer0_mux.clk.hw);

static const struct clk_parent_data k230_timer1_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer1_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer1_mux,
		    K230_TIMER1_MUX,
		    0x50, 8, 0x1,
		    0, 0,
		    k230_timer1_mux_pdata);

K230_CLK_GATE_FORMAT(timer1_gate,
		     K230_TIMER1_GATE,
		     0x50, 14, CLK_IGNORE_UNUSED, 0,
		     &timer1_mux.clk.hw);

static const struct clk_parent_data k230_timer2_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer2_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer2_mux,
		    K230_TIMER2_MUX,
		    0x50, 9, 0x1,
		    0, 0,
		    k230_timer2_mux_pdata);

K230_CLK_GATE_FORMAT(timer2_gate,
		     K230_TIMER2_GATE,
		     0x50, 15, CLK_IGNORE_UNUSED, 0,
		     &timer2_mux.clk.hw);

static const struct clk_parent_data k230_timer3_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer3_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer3_mux,
		    K230_TIMER3_MUX,
		    0x50, 10, 0x1,
		    0, 0,
		    k230_timer3_mux_pdata);

K230_CLK_GATE_FORMAT(timer3_gate,
		     K230_TIMER3_GATE,
		     0x50, 16, CLK_IGNORE_UNUSED, 0,
		     &timer3_mux.clk.hw);

static const struct clk_parent_data k230_timer4_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer4_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer4_mux,
		    K230_TIMER4_MUX,
		    0x50, 11, 0x1,
		    0, 0,
		    k230_timer4_mux_pdata);

K230_CLK_GATE_FORMAT(timer4_gate,
		     K230_TIMER4_GATE,
		     0x50, 17, CLK_IGNORE_UNUSED, 0,
		     &timer4_mux.clk.hw);

static const struct clk_parent_data k230_timer5_mux_pdata[] = {
	{ .fw_name = "timer-pulse-in", },
	{ .hw = &timer5_src_rate.clk.hw, },
};

K230_CLK_MUX_FORMAT(timer5_mux,
		    K230_TIMER5_MUX,
		    0x50, 12, 0x1,
		    0, 0,
		    k230_timer5_mux_pdata);

K230_CLK_GATE_FORMAT(timer5_gate,
		     K230_TIMER5_GATE,
		     0x50, 18, CLK_IGNORE_UNUSED, 0,
		     &timer5_mux.clk.hw);

K230_CLK_GATE_FORMAT(shrm_apb_gate,
		     K230_SHRM_APB_GATE,
		     0x5C, 0, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(shrm_apb_rate,
		     K230_SHRM_APB_RATE,
		     1, 1, 0, 0,
		     1, 8, 18, 0x7,
		     0x5C, 31, div, 0x0,
		     false, 0,
		     &shrm_apb_gate.clk.hw);

static const struct clk_parent_data k230_shrm_sram_mux_pdata[] = {
	{ .hw = &pll3_div2.hw, },
	{ .hw = &pll0_div2.hw, },
};

K230_CLK_MUX_FORMAT(shrm_sram_mux,
		    K230_SHRM_SRAM_MUX,
		    0x50, 14, 0x1,
		    0, 0,
		    k230_shrm_sram_mux_pdata);

K230_CLK_GATE_FORMAT(shrm_sram_gate,
		     K230_SHRM_SRAM_GATE,
		     0x5c, 10, CLK_IGNORE_UNUSED, 0,
		     &shrm_sram_mux.clk.hw);

K230_CLK_FIXED_FACTOR_FORMAT(shrm_sram_div2,
			     1, 2, 0,
			     &shrm_sram_gate.clk.hw);

K230_CLK_GATE_FORMAT(shrm_axi_slave_gate,
		     K230_SHRM_AXI_SLAVE_GATE,
		     0x5C, 11, CLK_IGNORE_UNUSED, 0,
		     &shrm_sram_div2.hw);

K230_CLK_GATE_FORMAT(shrm_axi_gate,
		     K230_SHRM_AXI_GATE,
		     0x5C, 12, 0, 0,
		     &pll0_div4.hw);

K230_CLK_GATE_FORMAT(shrm_nonai2d_axi_gate,
		     K230_SHRM_NONAI2D_AXI_GATE,
		     0x5C, 9, 0, 0,
		     &shrm_axi_gate.clk.hw);

K230_CLK_GATE_FORMAT(shrm_decompress_axi_gate,
		     K230_SHRM_DECOMPRESS_AXI_GATE,
		     0x5C, 7, CLK_IGNORE_UNUSED, 0,
		     &shrm_sram_gate.clk.hw);

K230_CLK_GATE_FORMAT(shrm_sdma_axi_gate,
		     K230_SHRM_SDMA_AXI_GATE,
		     0x5C, 5, 0, 0,
		     &shrm_axi_gate.clk.hw);

K230_CLK_GATE_FORMAT(shrm_pdma_axi_gate,
		     K230_SHRM_PDMA_AXI_GATE,
		     0x5C, 3, 0, 0,
		     &shrm_axi_gate.clk.hw);

static const struct clk_parent_data k230_ddrc_src_mux_pdata[] = {
	{ .hw = &pll0_div2.hw, },
	{ .hw = &pll0_div3.hw, },
	{ .hw = &pll2_div4.hw, },
};

K230_CLK_MUX_FORMAT(ddrc_src_mux,
		    K230_DDRC_SRC_MUX,
		    0x60, 0, 0x3,
		    0, 0,
		    k230_ddrc_src_mux_pdata);

K230_CLK_GATE_FORMAT(ddrc_src_gate,
		     K230_DDRC_SRC_GATE,
		     0x60, 2, CLK_IGNORE_UNUSED, 0,
		     &ddrc_src_mux.clk.hw);

K230_CLK_RATE_FORMAT(ddrc_src_rate,
		     K230_DDRC_SRC_RATE,
		     1, 1, 0, 0,
		     1, 16, 10, 0xF,
		     0x60, 31, div, 0x0,
		     false, 0,
		     &ddrc_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(ddrc_bypass_gate,
		     K230_DDRC_BYPASS_GATE,
		     0x60, 8, 0, 0,
		     &pll2_div4.hw);

K230_CLK_GATE_FORMAT(ddrc_apb_gate,
		     K230_DDRC_APB_GATE,
		     0x60, 9, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(ddrc_apb_rate,
		     K230_DDRC_APB_RATE,
		     1, 1, 0, 0,
		     1, 16, 14, 0xF,
		     0x60, 31, div, 0x0,
		     false, 0,
		     &ddrc_apb_gate.clk.hw);

K230_CLK_GATE_FORMAT(display_ahb_gate,
		     K230_DISPLAY_AHB_GATE,
		     0x74, 0, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(display_ahb_rate,
		     K230_DISPLAY_AHB_RATE,
		     1, 1, 0, 0,
		     1, 8, 0, 0x7,
		     0x78, 31, div, 0x0,
		     false, 0,
		     &display_ahb_gate.clk.hw);

K230_CLK_GATE_FORMAT(display_axi_gate,
		     K230_DISPLAY_AXI_GATE,
		     0x74, 1, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(display_clkext_rate,
		     K230_DISPLAY_CLKEXT_RATE,
		     1, 1, 0, 0,
		     1, 16, 16, 0xF,
		     0x78, 31, div, 0x0,
		     false, 0,
		     &pll0_div3.hw);

K230_CLK_GATE_FORMAT(display_gpu_gate,
		     K230_DISPLAY_GPU_GATE,
		     0x74, 6, 0, 0,
		     &pll0_div3.hw);

K230_CLK_RATE_FORMAT(display_gpu_rate,
		     K230_DISPLAY_GPU_RATE,
		     1, 1, 0, 0,
		     1, 16, 20, 0xF,
		     0x78, 31, div, 0x0,
		     false, 0,
		     &display_gpu_gate.clk.hw);

K230_CLK_GATE_FORMAT(display_dpip_gate,
		     K230_DISPLAY_DPIP_GATE,
		     0x74, 2, 0, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(display_dpip_rate,
		     K230_DISPLAY_DPIP_RATE,
		     1, 1, 0, 0,
		     1, 256, 3, 0xFF,
		     0x78, 31, div, 0x0,
		     false, 0,
		     &display_dpip_gate.clk.hw);

K230_CLK_GATE_FORMAT(display_cfg_gate,
		     K230_DISPLAY_CFG_GATE,
		     0x74, 4, 0, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(display_cfg_rate,
		     K230_DISPLAY_CFG_RATE,
		     1, 1, 0, 0,
		     1, 32, 11, 0x1F,
		     0x78, 31, div, 0x0,
		     false, 0,
		     &display_cfg_gate.clk.hw);

K230_CLK_GATE_FORMAT_PNAME(display_ref_gate,
			   K230_DISPLAY_REF_GATE,
			   0x74, 3, 0, 0,
			   "osc24m");

K230_CLK_GATE_FORMAT(vpu_src_gate,
		     K230_VPU_SRC_GATE,
		     0xC, 0, 0, 0,
		     &pll0_div2.hw);

K230_CLK_RATE_FORMAT(vpu_src_rate,
		     K230_VPU_SRC_RATE,
		     1, 16, 1, 0xF,
		     16, 16, 0, 0,
		     0x0, 31, mul, 0xC,
		     false, 0,
		     &vpu_src_gate.clk.hw);

K230_CLK_RATE_FORMAT(vpu_axi_src_rate,
		     K230_VPU_AXI_SRC_RATE,
		     1, 1, 0, 0,
		     1, 16, 6, 0xF,
		     0xC, 31, div, 0x0,
		     false, 0,
		     &vpu_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(vpu_axi_gate,
		     K230_VPU_AXI_GATE,
		     0xC, 5, 0, 0,
		     &vpu_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(vpu_ddrcp2_gate,
		     K230_VPU_DDRCP2_GATE,
		     0x60, 5, 0, 0,
		     &vpu_axi_src_rate.clk.hw);

K230_CLK_GATE_FORMAT(vpu_cfg_gate,
		     K230_VPU_CFG_GATE,
		     0xC, 10, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(vpu_cfg_rate,
		     K230_VPU_CFG_RATE,
		     1, 1, 0, 0,
		     1, 16, 11, 0xF,
		     0xC, 31, div, 0x0,
		     false, 0,
		     &vpu_cfg_gate.clk.hw);

K230_CLK_GATE_FORMAT(sec_apb_gate,
		     K230_SEC_APB_GATE,
		     0x80, 0, 0, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(sec_apb_rate,
		     K230_SEC_APB_RATE,
		     1, 1, 0, 0,
		     1, 8, 1, 0x7,
		     0x80, 31, div, 0x0,
		     false, 0,
		     &sec_apb_gate.clk.hw);

K230_CLK_GATE_FORMAT(sec_fix_gate,
		     K230_SEC_FIX_GATE,
		     0x80, 5, 0, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(sec_fix_rate,
		     K230_SEC_FIX_RATE,
		     1, 1, 0, 0,
		     1, 32, 6, 0x1F,
		     0x80, 31, div, 0x0,
		     false, 0,
		     &sec_fix_gate.clk.hw);

K230_CLK_GATE_FORMAT(sec_axi_gate,
		     K230_SEC_AXI_GATE,
		     0x80, 4, 0, 0,
		     &pll1_div4.hw);

K230_CLK_RATE_FORMAT(sec_axi_rate,
		     K230_SEC_AXI_RATE,
		     1, 1, 0, 0,
		     1, 8, 11, 0x3,
		     0x80, 31, div, 0,
		     false, 0,
		     &sec_axi_gate.clk.hw);

K230_CLK_GATE_FORMAT(usb_480m_gate,
		     K230_USB_480M_GATE,
		     0x100, 0, 0, 0,
		     &pll1.hw);

K230_CLK_RATE_FORMAT(usb_480m_rate,
		     K230_USB_480M_RATE,
		     1, 1, 0, 0,
		     1, 8, 1, 0x7,
		     0x100, 31, div, 0,
		     false, 0,
		     &usb_480m_gate.clk.hw);

K230_CLK_GATE_FORMAT(usb_100m_gate,
		     K230_USB_100M_GATE,
		     0x100, 0, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(usb_100m_rate,
		     K230_USB_100M_RATE,
		     1, 1, 0, 0,
		     1, 8, 4, 0x7,
		     0x100, 31, div, 0,
		     false, 0,
		     &usb_100m_gate.clk.hw);

K230_CLK_GATE_FORMAT(dphy_dft_gate,
		     K230_DPHY_DFT_GATE,
		     0x100, 0, 0, 0,
		     &pll0.hw);

K230_CLK_RATE_FORMAT(dphy_dft_rate,
		     K230_DPHY_DFT_RATE,
		     1, 1, 0, 0,
		     1, 16, 1, 0xF,
		     0x104, 31, div, 0,
		     false, 0,
		     &dphy_dft_gate.clk.hw);

K230_CLK_GATE_FORMAT(spi2axi_gate,
		     K230_SPI2AXI_GATE,
		     0x108, 0, 0, 0,
		     &pll0_div4.hw);

K230_CLK_RATE_FORMAT(spi2axi_rate,
		     K230_SPI2AXI_RATE,
		     1, 1, 0, 0,
		     1, 8, 1, 0x7,
		     0x108, 31, div, 0x0,
		     false, 0,
		     &spi2axi_gate.clk.hw);

static const struct clk_parent_data k230_ai_src_mux_pdata[] = {
	{ .hw = &pll0_div2.hw, },
	{ .hw = &pll3_div2.hw, },
};

K230_CLK_MUX_FORMAT(ai_src_mux,
		    K230_AI_SRC_MUX,
		    0x8, 2, 0x1,
		    0, 0,
		    k230_ai_src_mux_pdata);

K230_CLK_GATE_FORMAT(ai_src_gate,
		     K230_AI_SRC_GATE,
		     0x8, 0, CLK_IGNORE_UNUSED, 0,
		     &ai_src_mux.clk.hw);

K230_CLK_RATE_FORMAT(ai_src_rate,
		     K230_AI_SRC_RATE,
		     1, 1, 0, 0,
		     1, 8, 3, 0x7,
		     0x8, 31, div, 0x0,
		     false, 0,
		     &ai_src_gate.clk.hw);

K230_CLK_GATE_FORMAT(ai_axi_gate,
		     K230_AI_AXI_GATE,
		     0x8, 10, 0, 0,
		     &pll0_div4.hw);

static const struct clk_parent_data k230_camera0_mux_pdata[] = {
	{ .hw = &pll1_div3.hw, },
	{ .hw = &pll1_div4.hw, },
	{ .hw = &pll0_div4.hw, },
};

K230_CLK_MUX_FORMAT(camera0_mux,
		    K230_CAMERA0_MUX,
		    0x6C, 3, 0x3,
		    0, 0,
		    k230_camera0_mux_pdata);

K230_CLK_GATE_FORMAT(camera0_gate,
		     K230_CAMERA0_GATE,
		     0x6C, 0, CLK_IGNORE_UNUSED, 0,
		     &camera0_mux.clk.hw);

K230_CLK_RATE_FORMAT(camera0_rate,
		     K230_CAMERA0_RATE,
		     1, 1, 0, 0,
		     1, 32, 5, 0x1f,
		     0x6C, 31, div, 0x0,
		     false, 0,
		     &camera0_gate.clk.hw);

static const struct clk_parent_data k230_camera1_mux_pdata[] = {
	{ .hw = &pll1_div3.hw, },
	{ .hw = &pll1_div4.hw, },
	{ .hw = &pll0_div4.hw, },
};

K230_CLK_MUX_FORMAT(camera1_mux,
		    K230_CAMERA1_MUX,
		    0x6C, 10, 0x3,
		    0, 0,
		    k230_camera1_mux_pdata);

K230_CLK_GATE_FORMAT(camera1_gate,
		     K230_CAMERA1_GATE,
		     0x6C, 1, CLK_IGNORE_UNUSED, 0,
		     &camera1_mux.clk.hw);

K230_CLK_RATE_FORMAT(camera1_rate,
		     K230_CAMERA1_RATE,
		     1, 1, 0, 0,
		     1, 32, 12, 0x1f,
		     0x6C, 31, div, 0x0,
		     false, 0,
		     &camera1_gate.clk.hw);

static const struct clk_parent_data k230_camera2_mux_pdata[] = {
	{ .hw = &pll1_div3.hw, },
	{ .hw = &pll1_div4.hw, },
	{ .hw = &pll0_div4.hw, },
};

K230_CLK_MUX_FORMAT(camera2_mux,
		    K230_CAMERA2_MUX,
		    0x6C, 17, 0x3,
		    0, 0,
		    k230_camera2_mux_pdata);

K230_CLK_GATE_FORMAT(camera2_gate,
		     K230_CAMERA2_GATE,
		     0x6C, 2, CLK_IGNORE_UNUSED, 0,
		     &camera2_mux.clk.hw);

K230_CLK_RATE_FORMAT(camera2_rate,
		     K230_CAMERA2_RATE,
		     1, 1, 0, 0,
		     1, 32, 19, 0x1f,
		     0x6C, 31, div, 0x0,
		     false, 0,
		     &camera2_gate.clk.hw);

static struct k230_clk_mux *k230_clk_muxs[] = {
	&hs_ssi0_mux,
	&hs_usb_ref_mux,
	&cpu1_src_mux,
	&timer0_mux,
	&timer1_mux,
	&timer2_mux,
	&timer3_mux,
	&timer4_mux,
	&timer5_mux,
	&shrm_sram_mux,
	&ddrc_src_mux,
	&ai_src_mux,
	&camera0_mux,
	&camera1_mux,
	&camera2_mux,
};

#define K230_CLK_MUX_NUM ARRAY_SIZE(k230_clk_muxs)

static struct k230_clk_gate *k230_clk_gates[] = {
	&cpu0_src_gate,
	&cpu0_plic_gate,
	&cpu0_noc_ddrcp4_gate,
	&cpu0_apb_gate,
	&cpu1_src_gate,
	&cpu1_plic_gate,
	&cpu1_apb_gate,
	&pmu_apb_gate,
	&hs_hclk_high_gate,
	&hs_hclk_gate,
	&hs_sd0_ahb_gate,
	&hs_sd1_ahb_gate,
	&hs_ssi1_ahb_gate,
	&hs_ssi2_ahb_gate,
	&hs_usb0_ahb_gate,
	&hs_usb1_ahb_gate,
	&hs_ssi0_axi_gate,
	&hs_ssi1_gate,
	&hs_ssi2_gate,
	&hs_qspi_axi_src_gate,
	&hs_ssi1_axi_gate,
	&hs_ssi2_axi_gate,
	&hs_sd_card_src_gate,
	&hs_sd0_card_gate,
	&hs_sd1_card_gate,
	&hs_sd_axi_src_gate,
	&hs_sd0_axi_gate,
	&hs_sd1_axi_gate,
	&hs_sd0_base_gate,
	&hs_sd1_base_gate,
	&hs_ssi0_gate,
	&hs_sd_timer_src_gate,
	&hs_sd0_timer_gate,
	&hs_sd1_timer_gate,
	&hs_usb0_ref_gate,
	&hs_usb1_ref_gate,
	&ls_apb_src_gate,
	&ls_uart0_apb_gate,
	&ls_uart1_apb_gate,
	&ls_uart2_apb_gate,
	&ls_uart3_apb_gate,
	&ls_uart4_apb_gate,
	&ls_i2c0_apb_gate,
	&ls_i2c1_apb_gate,
	&ls_i2c2_apb_gate,
	&ls_i2c3_apb_gate,
	&ls_i2c4_apb_gate,
	&ls_gpio_apb_gate,
	&ls_pwm_apb_gate,
	&ls_jamlink0_apb_gate,
	&ls_jamlink1_apb_gate,
	&ls_jamlink2_apb_gate,
	&ls_jamlink3_apb_gate,
	&ls_audio_apb_gate,
	&ls_adc_apb_gate,
	&ls_codec_apb_gate,
	&ls_i2c0_gate,
	&ls_i2c1_gate,
	&ls_i2c2_gate,
	&ls_i2c3_gate,
	&ls_i2c4_gate,
	&ls_codec_adc_gate,
	&ls_codec_dac_gate,
	&ls_audio_dev_gate,
	&ls_pdm_gate,
	&ls_adc_gate,
	&ls_uart0_gate,
	&ls_uart1_gate,
	&ls_uart2_gate,
	&ls_uart3_gate,
	&ls_uart4_gate,
	&ls_jamlink0co_gate,
	&ls_jamlink1co_gate,
	&ls_jamlink2co_gate,
	&ls_jamlink3co_gate,
	&ls_gpio_debounce_gate,
	&sysctl_wdt0_apb_gate,
	&sysctl_wdt1_apb_gate,
	&sysctl_timer_apb_gate,
	&sysctl_iomux_apb_gate,
	&sysctl_mailbox_apb_gate,
	&sysctl_hdi_gate,
	&sysctl_time_stamp_gate,
	&sysctl_wdt0_gate,
	&sysctl_wdt1_gate,
	&timer0_gate,
	&timer1_gate,
	&timer2_gate,
	&timer3_gate,
	&timer4_gate,
	&timer5_gate,
	&shrm_apb_gate,
	&shrm_sram_gate,
	&shrm_axi_gate,
	&shrm_axi_slave_gate,
	&shrm_nonai2d_axi_gate,
	&shrm_decompress_axi_gate,
	&shrm_sdma_axi_gate,
	&shrm_pdma_axi_gate,
	&ddrc_src_gate,
	&ddrc_bypass_gate,
	&ddrc_apb_gate,
	&display_ahb_gate,
	&display_axi_gate,
	&display_gpu_gate,
	&display_dpip_gate,
	&display_cfg_gate,
	&display_ref_gate,
	&vpu_src_gate,
	&vpu_axi_gate,
	&vpu_ddrcp2_gate,
	&vpu_cfg_gate,
	&sec_apb_gate,
	&sec_fix_gate,
	&sec_axi_gate,
	&usb_480m_gate,
	&usb_100m_gate,
	&dphy_dft_gate,
	&spi2axi_gate,
	&ai_src_gate,
	&ai_axi_gate,
	&camera0_gate,
	&camera1_gate,
	&camera2_gate,
};

#define K230_CLK_GATE_NUM ARRAY_SIZE(k230_clk_gates)

static struct k230_clk_rate *k230_clk_rates[] = {
	&cpu0_src_rate,
	&cpu0_axi_rate,
	&cpu0_plic_rate,
	&cpu0_apb_rate,
	&cpu1_src_rate,
	&cpu1_axi_rate,
	&cpu1_plic_rate,
	&hs_hclk_high_rate,
	&hs_hclk_rate,
	&hs_ssi0_axi_rate,
	&hs_ssi1_rate,
	&hs_ssi2_rate,
	&hs_qspi_axi_src_rate,
	&hs_sd_card_src_rate,
	&hs_sd_axi_src_rate,
	&hs_usb_ref_50m_rate,
	&hs_sd_timer_src_rate,
	&ls_apb_src_rate,
	&ls_gpio_debounce_rate,
	&ls_i2c0_rate,
	&ls_i2c1_rate,
	&ls_i2c2_rate,
	&ls_i2c3_rate,
	&ls_i2c4_rate,
	&ls_codec_adc_rate,
	&ls_codec_dac_rate,
	&ls_audio_dev_rate,
	&ls_pdm_rate,
	&ls_adc_rate,
	&ls_uart0_rate,
	&ls_uart1_rate,
	&ls_uart2_rate,
	&ls_uart3_rate,
	&ls_uart4_rate,
	&ls_jamlinkco_src_rate,
	&sysctl_hdi_rate,
	&sysctl_time_stamp_rate,
	&sysctl_temp_sensor_rate,
	&sysctl_wdt0_rate,
	&sysctl_wdt1_rate,
	&timer0_src_rate,
	&timer1_src_rate,
	&timer2_src_rate,
	&timer3_src_rate,
	&timer4_src_rate,
	&timer5_src_rate,
	&shrm_apb_rate,
	&ddrc_src_rate,
	&ddrc_apb_rate,
	&display_ahb_rate,
	&display_clkext_rate,
	&display_gpu_rate,
	&display_dpip_rate,
	&display_cfg_rate,
	&vpu_src_rate,
	&vpu_axi_src_rate,
	&vpu_cfg_rate,
	&sec_apb_rate,
	&sec_fix_rate,
	&sec_axi_rate,
	&usb_480m_rate,
	&usb_100m_rate,
	&dphy_dft_rate,
	&spi2axi_rate,
	&ai_src_rate,
	&camera0_rate,
	&camera1_rate,
	&camera2_rate,
};

#define K230_CLK_RATE_NUM ARRAY_SIZE(k230_clk_rates)

#define K230_CLK_NUM (K230_CLK_MUX_NUM + K230_CLK_GATE_NUM + K230_CLK_RATE_NUM + 1)

static int k230_pll_prepare(struct clk_hw *hw)
{
	struct k230_pll *pll = hw_to_k230_pll(hw);
	u32 reg;

	/* wait for PLL lock until it reaches lock status */
	return readl_poll_timeout(K230_PLLX_LOCK_ADDR(pll->reg, pll->id), reg,
				  reg & K230_PLL_LOCK_STATUS_MASK,
				  K230_PLL_LOCK_TIME_DELAY, K230_PLL_LOCK_TIMEOUT);
}

static inline bool k230_pll_hw_is_enabled(struct k230_pll *pll)
{
	return readl(K230_PLLX_GATE_ADDR(pll->reg, pll->id)) & K230_PLL_GATE_ENABLE;
}

static void k230_pll_enable_hw(struct k230_pll *pll)
{
	u32 reg;

	if (k230_pll_hw_is_enabled(pll))
		return;

	reg = readl(K230_PLLX_GATE_ADDR(pll->reg, pll->id));
	reg |= K230_PLL_GATE_ENABLE | K230_PLL_GATE_WRITE_ENABLE;
	writel(reg, K230_PLLX_GATE_ADDR(pll->reg, pll->id));
}

static int k230_pll_enable(struct clk_hw *hw)
{
	struct k230_pll *pll = hw_to_k230_pll(hw);

	guard(spinlock)(pll->lock);

	k230_pll_enable_hw(pll);

	return 0;
}

static void k230_pll_disable(struct clk_hw *hw)
{
	struct k230_pll *pll = hw_to_k230_pll(hw);
	u32 reg;

	guard(spinlock)(pll->lock);

	reg = readl(K230_PLLX_GATE_ADDR(pll->reg, pll->id));
	reg &= ~(K230_PLL_GATE_ENABLE);
	reg |= (K230_PLL_GATE_WRITE_ENABLE);
	writel(reg, K230_PLLX_GATE_ADDR(pll->reg, pll->id));
}

static int k230_pll_is_enabled(struct clk_hw *hw)
{
	return k230_pll_hw_is_enabled(hw_to_k230_pll(hw));
}

static unsigned long k230_pll_get_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct k230_pll *pll = hw_to_k230_pll(hw);
	u32 reg;
	u32 r, f, od;

	guard(spinlock)(pll->lock);

	reg = readl(K230_PLLX_BYPASS_ADDR(pll->reg, pll->id));
	if (reg & K230_PLL_BYPASS_ENABLE)
		return parent_rate;

	reg = readl(K230_PLLX_LOCK_ADDR(pll->reg, pll->id));
	if (!(reg & (K230_PLL_LOCK_STATUS_MASK)))
		return 0;

	reg = readl(K230_PLLX_DIV_ADDR(pll->reg, pll->id));
	r = FIELD_GET(K230_PLL_R_MASK, reg) + 1;
	f = FIELD_GET(K230_PLL_F_MASK, reg) + 1;
	od = FIELD_GET(K230_PLL_OD_MASK, reg) + 1;

	return mul_u64_u32_div(parent_rate, f, r * od);
}

static int k230_register_plls(struct platform_device *pdev, spinlock_t *lock,
			      void __iomem *reg)
{
	int i, ret;
	struct k230_pll *pll;

	for (i = 0; i < ARRAY_SIZE(k230_plls); i++) {
		const char *name;

		pll = k230_plls[i];

		name = pll->hw.init->name;
		pll->lock = lock;
		pll->reg = reg;

		ret = devm_clk_hw_register(&pdev->dev, &pll->hw);
		if (ret)
			return ret;

		ret = devm_clk_hw_register_clkdev(&pdev->dev, &pll->hw, name, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static int k230_register_pll_divs(struct platform_device *pdev)
{
	struct clk_fixed_factor *pll_div;
	int ret;

	for (int i = 0; i < ARRAY_SIZE(k230_pll_divs); i++) {
		const char *name;

		pll_div = k230_pll_divs[i];

		name = pll_div->hw.init->name;

		ret = devm_clk_hw_register(&pdev->dev, &pll_div->hw);
		if (ret)
			return ret;

		ret = devm_clk_hw_register_clkdev(&pdev->dev, &pll_div->hw,
						  name, NULL);
		if (ret)
			return ret;
	}

	return 0;
}

static unsigned long k230_clk_get_rate_mul(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 mul, div;

	guard(spinlock)(rate_self->lock);

	div = rate_self->div_max;
	mul = (readl(rate_self->reg + clk->mul_reg_off) >> rate_self->mul_shift)
	      & rate_self->mul_mask;

	return mul_u64_u32_div(parent_rate, mul + 1, div);
}

static unsigned long k230_clk_get_rate_div(struct clk_hw *hw,
					   unsigned long parent_rate)
{
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 mul, div;

	guard(spinlock)(rate_self->lock);

	mul = rate_self->mul_max;
	div = (readl(rate_self->reg + clk->div_reg_off) >> rate_self->div_shift)
	      & rate_self->div_mask;

	return mul_u64_u32_div(parent_rate, mul, div + 1);
}

static unsigned long k230_clk_get_rate_mul_div(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 mul, div;

	guard(spinlock)(rate_self->lock);

	div = (readl(rate_self->reg + clk->div_reg_off) >> rate_self->div_shift)
	      & rate_self->div_mask;
	mul = (readl(rate_self->reg + clk->mul_reg_off) >> rate_self->mul_shift)
	      & rate_self->mul_mask;

	return mul_u64_u32_div(parent_rate, mul, div);
}

static int k230_clk_find_approximate_mul(u32 mul_min, u32 mul_max,
					 u32 div_min, u32 div_max,
					 unsigned long rate, unsigned long parent_rate,
					 u32 *div, u32 *mul)
{
	long abs_min;
	long abs_current;
	long perfect_divide;

	if (!rate || !parent_rate || !mul_min)
		return -EINVAL;

	perfect_divide = (long)((parent_rate * 1000) / rate);
	abs_min = abs(perfect_divide -
		     (long)(((long)div_max * 1000) / (long)mul_min));
	*mul = mul_min;

	for (u32 i = mul_min + 1; i <= mul_max; i++) {
		abs_current = abs(perfect_divide -
				(long)(((long)div_max * 1000) / (long)i));

		if (abs_min > abs_current) {
			abs_min = abs_current;
			*mul = i;
		}
	}

	*div = div_max;

	return 0;
}

static int k230_clk_find_approximate_div(u32 mul_min, u32 mul_max,
					 u32 div_min, u32 div_max,
					 unsigned long rate, unsigned long parent_rate,
					 u32 *div, u32 *mul)
{
	long abs_min;
	long abs_current;
	long perfect_divide;

	if (!rate || !parent_rate || !mul_max)
		return -EINVAL;

	perfect_divide = (long)((parent_rate * 1000) / rate);
	abs_min = abs(perfect_divide -
		     (long)(((long)div_min * 1000) / (long)mul_max));
	*div = div_min;

	for (u32 i = div_min + 1; i <= div_max; i++) {
		abs_current = abs(perfect_divide -
				 (long)(((long)i * 1000) / (long)mul_max));

		if (abs_min > abs_current) {
			abs_min = abs_current;
			*div = i;
		}
	}

	*mul = mul_max;

	return 0;
}

static int k230_clk_find_approximate_mul_div(u32 mul_min, u32 mul_max,
					     u32 div_min, u32 div_max,
					     unsigned long rate,
					     unsigned long parent_rate,
					     u32 *div, u32 *mul)
{
	unsigned long best_mul, best_div;

	if (!rate || !parent_rate || !mul_min)
		return -EINVAL;

	rational_best_approximation(rate, parent_rate,
				    (unsigned long)mul_max, (unsigned long)div_max,
				    &best_mul, &best_div);

	if (best_mul < mul_min)
		best_mul = mul_min;

	if (best_div < div_min)
		best_div = div_min;

	*mul = (u32)best_mul;
	*div = (u32)best_div;

	return 0;
}

static int k230_clk_determine_rate_mul(struct clk_hw *hw, struct clk_rate_request *req)
{
	int ret;
	struct k230_clk_rate_self *rate_self = hw_to_k230_clk_rate_self(hw);
	unsigned long rate = req->rate;
	unsigned long parent_rate = req->best_parent_rate;
	u32 div, mul;

	ret = k230_clk_find_approximate_mul(rate_self->mul_min, rate_self->mul_max,
					    rate_self->div_min, rate_self->div_max,
					    rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	req->rate = mul_u64_u32_div(parent_rate, mul, div);

	return 0;
}

static int k230_clk_determine_rate_div(struct clk_hw *hw, struct clk_rate_request *req)
{
	int ret;
	struct k230_clk_rate_self *rate_self = hw_to_k230_clk_rate_self(hw);
	unsigned long rate = req->rate;
	unsigned long parent_rate = req->best_parent_rate;
	u32 div, mul;

	ret = k230_clk_find_approximate_div(rate_self->mul_min, rate_self->mul_max,
					    rate_self->div_min, rate_self->div_max,
					    rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	req->rate = mul_u64_u32_div(parent_rate, mul, div);

	return 0;
}

static int k230_clk_determine_rate_mul_div(struct clk_hw *hw, struct clk_rate_request *req)
{
	int ret;
	struct k230_clk_rate_self *rate_self = hw_to_k230_clk_rate_self(hw);
	unsigned long rate = req->rate;
	unsigned long parent_rate = req->best_parent_rate;
	u32 div, mul;

	ret = k230_clk_find_approximate_mul_div(rate_self->mul_min, rate_self->mul_max,
						rate_self->div_min, rate_self->div_max,
						rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	req->rate = mul_u64_u32_div(parent_rate, mul, div);

	return 0;
}

static int k230_clk_set_rate_mul(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	int ret;
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 div, mul, mul_reg;

	if (rate > parent_rate)
		return -EINVAL;

	if (rate_self->read_only)
		return 0;

	ret = k230_clk_find_approximate_mul(rate_self->mul_min, rate_self->mul_max,
					    rate_self->div_min, rate_self->div_max,
					    rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	guard(spinlock)(rate_self->lock);

	mul_reg = readl(rate_self->reg + clk->mul_reg_off);
	mul_reg |= ((mul - 1) & rate_self->mul_mask) << (rate_self->mul_shift);
	mul_reg |= BIT(rate_self->write_enable_bit);
	writel(mul_reg, rate_self->reg + clk->mul_reg_off);

	return 0;
}

static int k230_clk_set_rate_div(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	int ret;
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 div, mul, div_reg;

	if (rate > parent_rate)
		return -EINVAL;

	if (rate_self->read_only)
		return 0;

	ret = k230_clk_find_approximate_div(rate_self->mul_min, rate_self->mul_max,
					    rate_self->div_min, rate_self->div_max,
					    rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	guard(spinlock)(rate_self->lock);

	div_reg = readl(rate_self->reg + clk->div_reg_off);
	div_reg |= ((div - 1) & rate_self->div_mask) << (rate_self->div_shift);
	div_reg |= BIT(rate_self->write_enable_bit);
	writel(div_reg, rate_self->reg + clk->div_reg_off);

	return 0;
}

static int k230_clk_set_rate_mul_div(struct clk_hw *hw, unsigned long rate,
				     unsigned long parent_rate)
{
	int ret;
	struct k230_clk_rate *clk = hw_to_k230_clk_rate(hw);
	struct k230_clk_rate_self *rate_self = &clk->clk;
	u32 div, mul, div_reg, mul_reg;

	if (rate > parent_rate)
		return -EINVAL;

	if (rate_self->read_only)
		return 0;

	ret = k230_clk_find_approximate_mul_div(rate_self->mul_min, rate_self->mul_max,
						rate_self->div_min, rate_self->div_max,
						rate, parent_rate, &div, &mul);
	if (ret)
		return ret;

	guard(spinlock)(rate_self->lock);

	div_reg = readl(rate_self->reg + clk->div_reg_off);
	div_reg |= ((div - 1) & rate_self->div_mask) << (rate_self->div_shift);
	div_reg |= BIT(rate_self->write_enable_bit);
	writel(div_reg, rate_self->reg + clk->div_reg_off);

	mul_reg = readl(rate_self->reg + clk->mul_reg_off);
	mul_reg |= ((mul - 1) & rate_self->mul_mask) << (rate_self->mul_shift);
	mul_reg |= BIT(rate_self->write_enable_bit);
	writel(mul_reg, rate_self->reg + clk->mul_reg_off);

	return 0;
}

static int k230_register_clk(int id, struct clk_hw *hw, struct device *dev,
			     struct clk_hw_onecell_data *hw_data)
{
	int ret;

	ret = devm_clk_hw_register(dev, hw);
	if (ret)
		return ret;

	hw_data->hws[id] = hw;

	return 0;
}

static int k230_register_clks(struct platform_device *pdev,
			      struct clk_hw_onecell_data *hw_data,
			      spinlock_t *lock, void __iomem *reg)
{
	int i, ret;
	struct device *dev = &pdev->dev;
	struct clk_fixed_factor *fixed_factor = &shrm_sram_div2;
	struct k230_clk_mux *mux;
	struct k230_clk_gate *gate;
	struct k230_clk_rate *rate;

	for (i = 0; i < K230_CLK_MUX_NUM; i++) {
		mux = k230_clk_muxs[i];
		mux->clk.lock = lock;
		mux->clk.reg = reg + mux->reg_off;

		ret = k230_register_clk(mux->id, &mux->clk.hw, dev, hw_data);
		if (ret)
			return ret;
	}

	for (i = 0; i < K230_CLK_GATE_NUM; i++) {
		gate = k230_clk_gates[i];
		gate->clk.lock = lock;
		gate->clk.reg = reg + gate->reg_off;

		ret = k230_register_clk(gate->id, &gate->clk.hw, dev, hw_data);
		if (ret)
			return ret;
	}

	for (i = 0; i < K230_CLK_RATE_NUM; i++) {
		rate = k230_clk_rates[i];
		rate->clk.lock = lock;
		rate->clk.reg = reg;

		ret = k230_register_clk(rate->id, &rate->clk.hw, dev, hw_data);
		if (ret)
			return ret;
	}

	ret = k230_register_clk(K230_SHRM_SRAM_DIV2, &fixed_factor->hw, dev, hw_data);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get, hw_data);
}

static int k230_clk_init_plls(struct platform_device *pdev)
{
	int ret;
	void __iomem *reg;
	/* used for all the plls */
	spinlock_t *lock;

	lock = devm_kzalloc(&pdev->dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;

	spin_lock_init(lock);

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ret = k230_register_plls(pdev, lock, reg);
	if (ret)
		return ret;

	ret = k230_register_pll_divs(pdev);
	if (ret)
		return ret;

	return 0;
}

static int k230_clk_init_clks(struct platform_device *pdev,
			      struct clk_hw_onecell_data *hw_data)
{
	int ret;
	void __iomem *reg;
	/* used for all the clocks */
	spinlock_t *lock;

	lock = devm_kzalloc(&pdev->dev, sizeof(*lock), GFP_KERNEL);
	if (!lock)
		return -ENOMEM;

	spin_lock_init(lock);

	hw_data->num = K230_CLK_NUM;

	reg = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	ret = k230_register_clks(pdev, hw_data, lock, reg);
	if (ret)
		return ret;

	return 0;
}

static int k230_clk_probe(struct platform_device *pdev)
{
	int ret;
	struct clk_hw_onecell_data *hw_data;

	hw_data = devm_kzalloc(&pdev->dev, struct_size(hw_data, hws, K230_CLK_NUM),
			       GFP_KERNEL);
	if (!hw_data)
		return -ENOMEM;

	ret = k230_clk_init_plls(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "init plls failed\n");

	ret = k230_clk_init_clks(pdev, hw_data);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "init clks failed\n");

	return 0;
}

static const struct of_device_id k230_clk_ids[] = {
	{ .compatible = "canaan,k230-clk" },
	{ /* Sentinel */ }
};

static struct platform_driver k230_clk_driver = {
	.driver = {
		.name = "k230_clock_controller",
		.of_match_table = k230_clk_ids,
	},
	.probe = k230_clk_probe,
};
builtin_platform_driver(k230_clk_driver);
