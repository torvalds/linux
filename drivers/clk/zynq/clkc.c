/*
 * Zynq clock controller
 *
 *  Copyright (C) 2012 - 2013 Xilinx
 *
 *  Sören Brinkmann <soren.brinkmann@xilinx.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk/zynq.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/io.h>

static void __iomem *zynq_slcr_base_priv;

#define SLCR_ARMPLL_CTRL		(zynq_slcr_base_priv + 0x100)
#define SLCR_DDRPLL_CTRL		(zynq_slcr_base_priv + 0x104)
#define SLCR_IOPLL_CTRL			(zynq_slcr_base_priv + 0x108)
#define SLCR_PLL_STATUS			(zynq_slcr_base_priv + 0x10c)
#define SLCR_ARM_CLK_CTRL		(zynq_slcr_base_priv + 0x120)
#define SLCR_DDR_CLK_CTRL		(zynq_slcr_base_priv + 0x124)
#define SLCR_DCI_CLK_CTRL		(zynq_slcr_base_priv + 0x128)
#define SLCR_APER_CLK_CTRL		(zynq_slcr_base_priv + 0x12c)
#define SLCR_GEM0_CLK_CTRL		(zynq_slcr_base_priv + 0x140)
#define SLCR_GEM1_CLK_CTRL		(zynq_slcr_base_priv + 0x144)
#define SLCR_SMC_CLK_CTRL		(zynq_slcr_base_priv + 0x148)
#define SLCR_LQSPI_CLK_CTRL		(zynq_slcr_base_priv + 0x14c)
#define SLCR_SDIO_CLK_CTRL		(zynq_slcr_base_priv + 0x150)
#define SLCR_UART_CLK_CTRL		(zynq_slcr_base_priv + 0x154)
#define SLCR_SPI_CLK_CTRL		(zynq_slcr_base_priv + 0x158)
#define SLCR_CAN_CLK_CTRL		(zynq_slcr_base_priv + 0x15c)
#define SLCR_CAN_MIOCLK_CTRL		(zynq_slcr_base_priv + 0x160)
#define SLCR_DBG_CLK_CTRL		(zynq_slcr_base_priv + 0x164)
#define SLCR_PCAP_CLK_CTRL		(zynq_slcr_base_priv + 0x168)
#define SLCR_FPGA0_CLK_CTRL		(zynq_slcr_base_priv + 0x170)
#define SLCR_621_TRUE			(zynq_slcr_base_priv + 0x1c4)
#define SLCR_SWDT_CLK_SEL		(zynq_slcr_base_priv + 0x304)

#define NUM_MIO_PINS	54

enum zynq_clk {
	armpll, ddrpll, iopll,
	cpu_6or4x, cpu_3or2x, cpu_2x, cpu_1x,
	ddr2x, ddr3x, dci,
	lqspi, smc, pcap, gem0, gem1, fclk0, fclk1, fclk2, fclk3, can0, can1,
	sdio0, sdio1, uart0, uart1, spi0, spi1, dma,
	usb0_aper, usb1_aper, gem0_aper, gem1_aper,
	sdio0_aper, sdio1_aper, spi0_aper, spi1_aper, can0_aper, can1_aper,
	i2c0_aper, i2c1_aper, uart0_aper, uart1_aper, gpio_aper, lqspi_aper,
	smc_aper, swdt, dbg_trc, dbg_apb, clk_max};

static struct clk *ps_clk;
static struct clk *clks[clk_max];
static struct clk_onecell_data clk_data;

static DEFINE_SPINLOCK(armpll_lock);
static DEFINE_SPINLOCK(ddrpll_lock);
static DEFINE_SPINLOCK(iopll_lock);
static DEFINE_SPINLOCK(armclk_lock);
static DEFINE_SPINLOCK(ddrclk_lock);
static DEFINE_SPINLOCK(dciclk_lock);
static DEFINE_SPINLOCK(gem0clk_lock);
static DEFINE_SPINLOCK(gem1clk_lock);
static DEFINE_SPINLOCK(canclk_lock);
static DEFINE_SPINLOCK(canmioclk_lock);
static DEFINE_SPINLOCK(dbgclk_lock);
static DEFINE_SPINLOCK(aperclk_lock);

static const char dummy_nm[] __initconst = "dummy_name";

static const char *armpll_parents[] __initdata = {"armpll_int", "ps_clk"};
static const char *ddrpll_parents[] __initdata = {"ddrpll_int", "ps_clk"};
static const char *iopll_parents[] __initdata = {"iopll_int", "ps_clk"};
static const char *gem0_mux_parents[] __initdata = {"gem0_div1", dummy_nm};
static const char *gem1_mux_parents[] __initdata = {"gem1_div1", dummy_nm};
static const char *can0_mio_mux2_parents[] __initdata = {"can0_gate",
	"can0_mio_mux"};
static const char *can1_mio_mux2_parents[] __initdata = {"can1_gate",
	"can1_mio_mux"};
static const char *dbg_emio_mux_parents[] __initdata = {"dbg_div",
	dummy_nm};

static const char *dbgtrc_emio_input_names[] __initdata = {"trace_emio_clk"};
static const char *gem0_emio_input_names[] __initdata = {"gem0_emio_clk"};
static const char *gem1_emio_input_names[] __initdata = {"gem1_emio_clk"};
static const char *swdt_ext_clk_input_names[] __initdata = {"swdt_ext_clk"};

static void __init zynq_clk_register_fclk(enum zynq_clk fclk,
		const char *clk_name, void __iomem *fclk_ctrl_reg,
		const char **parents)
{
	struct clk *clk;
	char *mux_name;
	char *div0_name;
	char *div1_name;
	spinlock_t *fclk_lock;
	spinlock_t *fclk_gate_lock;
	void __iomem *fclk_gate_reg = fclk_ctrl_reg + 8;

	fclk_lock = kmalloc(sizeof(*fclk_lock), GFP_KERNEL);
	if (!fclk_lock)
		goto err;
	fclk_gate_lock = kmalloc(sizeof(*fclk_gate_lock), GFP_KERNEL);
	if (!fclk_gate_lock)
		goto err;
	spin_lock_init(fclk_lock);
	spin_lock_init(fclk_gate_lock);

	mux_name = kasprintf(GFP_KERNEL, "%s_mux", clk_name);
	div0_name = kasprintf(GFP_KERNEL, "%s_div0", clk_name);
	div1_name = kasprintf(GFP_KERNEL, "%s_div1", clk_name);

	clk = clk_register_mux(NULL, mux_name, parents, 4, 0,
			fclk_ctrl_reg, 4, 2, 0, fclk_lock);

	clk = clk_register_divider(NULL, div0_name, mux_name,
			0, fclk_ctrl_reg, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, fclk_lock);

	clk = clk_register_divider(NULL, div1_name, div0_name,
			CLK_SET_RATE_PARENT, fclk_ctrl_reg, 20, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			fclk_lock);

	clks[fclk] = clk_register_gate(NULL, clk_name,
			div1_name, CLK_SET_RATE_PARENT, fclk_gate_reg,
			0, CLK_GATE_SET_TO_DISABLE, fclk_gate_lock);
	kfree(mux_name);
	kfree(div0_name);
	kfree(div1_name);

	return;

err:
	clks[fclk] = ERR_PTR(-ENOMEM);
}

static void __init zynq_clk_register_periph_clk(enum zynq_clk clk0,
		enum zynq_clk clk1, const char *clk_name0,
		const char *clk_name1, void __iomem *clk_ctrl,
		const char **parents, unsigned int two_gates)
{
	struct clk *clk;
	char *mux_name;
	char *div_name;
	spinlock_t *lock;

	lock = kmalloc(sizeof(*lock), GFP_KERNEL);
	if (!lock)
		goto err;
	spin_lock_init(lock);

	mux_name = kasprintf(GFP_KERNEL, "%s_mux", clk_name0);
	div_name = kasprintf(GFP_KERNEL, "%s_div", clk_name0);

	clk = clk_register_mux(NULL, mux_name, parents, 4, 0,
			clk_ctrl, 4, 2, 0, lock);

	clk = clk_register_divider(NULL, div_name, mux_name, 0, clk_ctrl, 8, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, lock);

	clks[clk0] = clk_register_gate(NULL, clk_name0, div_name,
			CLK_SET_RATE_PARENT, clk_ctrl, 0, 0, lock);
	if (two_gates)
		clks[clk1] = clk_register_gate(NULL, clk_name1, div_name,
				CLK_SET_RATE_PARENT, clk_ctrl, 1, 0, lock);

	kfree(mux_name);
	kfree(div_name);

	return;

err:
	clks[clk0] = ERR_PTR(-ENOMEM);
	if (two_gates)
		clks[clk1] = ERR_PTR(-ENOMEM);
}

static void __init zynq_clk_setup(struct device_node *np)
{
	int i;
	u32 tmp;
	int ret;
	struct clk *clk;
	char *clk_name;
	const char *clk_output_name[clk_max];
	const char *cpu_parents[4];
	const char *periph_parents[4];
	const char *swdt_ext_clk_mux_parents[2];
	const char *can_mio_mux_parents[NUM_MIO_PINS];

	pr_info("Zynq clock init\n");

	/* get clock output names from DT */
	for (i = 0; i < clk_max; i++) {
		if (of_property_read_string_index(np, "clock-output-names",
				  i, &clk_output_name[i])) {
			pr_err("%s: clock output name not in DT\n", __func__);
			BUG();
		}
	}
	cpu_parents[0] = clk_output_name[armpll];
	cpu_parents[1] = clk_output_name[armpll];
	cpu_parents[2] = clk_output_name[ddrpll];
	cpu_parents[3] = clk_output_name[iopll];
	periph_parents[0] = clk_output_name[iopll];
	periph_parents[1] = clk_output_name[iopll];
	periph_parents[2] = clk_output_name[armpll];
	periph_parents[3] = clk_output_name[ddrpll];

	/* ps_clk */
	ret = of_property_read_u32(np, "ps-clk-frequency", &tmp);
	if (ret) {
		pr_warn("ps_clk frequency not specified, using 33 MHz.\n");
		tmp = 33333333;
	}
	ps_clk = clk_register_fixed_rate(NULL, "ps_clk", NULL, CLK_IS_ROOT,
			tmp);

	/* PLLs */
	clk = clk_register_zynq_pll("armpll_int", "ps_clk", SLCR_ARMPLL_CTRL,
			SLCR_PLL_STATUS, 0, &armpll_lock);
	clks[armpll] = clk_register_mux(NULL, clk_output_name[armpll],
			armpll_parents, 2, 0, SLCR_ARMPLL_CTRL, 4, 1, 0,
			&armpll_lock);

	clk = clk_register_zynq_pll("ddrpll_int", "ps_clk", SLCR_DDRPLL_CTRL,
			SLCR_PLL_STATUS, 1, &ddrpll_lock);
	clks[ddrpll] = clk_register_mux(NULL, clk_output_name[ddrpll],
			ddrpll_parents, 2, 0, SLCR_DDRPLL_CTRL, 4, 1, 0,
			&ddrpll_lock);

	clk = clk_register_zynq_pll("iopll_int", "ps_clk", SLCR_IOPLL_CTRL,
			SLCR_PLL_STATUS, 2, &iopll_lock);
	clks[iopll] = clk_register_mux(NULL, clk_output_name[iopll],
			iopll_parents, 2, 0, SLCR_IOPLL_CTRL, 4, 1, 0,
			&iopll_lock);

	/* CPU clocks */
	tmp = readl(SLCR_621_TRUE) & 1;
	clk = clk_register_mux(NULL, "cpu_mux", cpu_parents, 4, 0,
			SLCR_ARM_CLK_CTRL, 4, 2, 0, &armclk_lock);
	clk = clk_register_divider(NULL, "cpu_div", "cpu_mux", 0,
			SLCR_ARM_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &armclk_lock);

	clks[cpu_6or4x] = clk_register_gate(NULL, clk_output_name[cpu_6or4x],
			"cpu_div", CLK_SET_RATE_PARENT | CLK_IGNORE_UNUSED,
			SLCR_ARM_CLK_CTRL, 24, 0, &armclk_lock);

	clk = clk_register_fixed_factor(NULL, "cpu_3or2x_div", "cpu_div", 0,
			1, 2);
	clks[cpu_3or2x] = clk_register_gate(NULL, clk_output_name[cpu_3or2x],
			"cpu_3or2x_div", CLK_IGNORE_UNUSED,
			SLCR_ARM_CLK_CTRL, 25, 0, &armclk_lock);

	clk = clk_register_fixed_factor(NULL, "cpu_2x_div", "cpu_div", 0, 1,
			2 + tmp);
	clks[cpu_2x] = clk_register_gate(NULL, clk_output_name[cpu_2x],
			"cpu_2x_div", CLK_IGNORE_UNUSED, SLCR_ARM_CLK_CTRL,
			26, 0, &armclk_lock);

	clk = clk_register_fixed_factor(NULL, "cpu_1x_div", "cpu_div", 0, 1,
			4 + 2 * tmp);
	clks[cpu_1x] = clk_register_gate(NULL, clk_output_name[cpu_1x],
			"cpu_1x_div", CLK_IGNORE_UNUSED, SLCR_ARM_CLK_CTRL, 27,
			0, &armclk_lock);

	/* Timers */
	swdt_ext_clk_mux_parents[0] = clk_output_name[cpu_1x];
	for (i = 0; i < ARRAY_SIZE(swdt_ext_clk_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				swdt_ext_clk_input_names[i]);
		if (idx >= 0)
			swdt_ext_clk_mux_parents[i + 1] =
				of_clk_get_parent_name(np, idx);
		else
			swdt_ext_clk_mux_parents[i + 1] = dummy_nm;
	}
	clks[swdt] = clk_register_mux(NULL, clk_output_name[swdt],
			swdt_ext_clk_mux_parents, 2, CLK_SET_RATE_PARENT,
			SLCR_SWDT_CLK_SEL, 0, 1, 0, &gem0clk_lock);

	/* DDR clocks */
	clk = clk_register_divider(NULL, "ddr2x_div", "ddrpll", 0,
			SLCR_DDR_CLK_CTRL, 26, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &ddrclk_lock);
	clks[ddr2x] = clk_register_gate(NULL, clk_output_name[ddr2x],
			"ddr2x_div", 0, SLCR_DDR_CLK_CTRL, 1, 0, &ddrclk_lock);
	clk_prepare_enable(clks[ddr2x]);
	clk = clk_register_divider(NULL, "ddr3x_div", "ddrpll", 0,
			SLCR_DDR_CLK_CTRL, 20, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &ddrclk_lock);
	clks[ddr3x] = clk_register_gate(NULL, clk_output_name[ddr3x],
			"ddr3x_div", 0, SLCR_DDR_CLK_CTRL, 0, 0, &ddrclk_lock);
	clk_prepare_enable(clks[ddr3x]);

	clk = clk_register_divider(NULL, "dci_div0", "ddrpll", 0,
			SLCR_DCI_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &dciclk_lock);
	clk = clk_register_divider(NULL, "dci_div1", "dci_div0",
			CLK_SET_RATE_PARENT, SLCR_DCI_CLK_CTRL, 20, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&dciclk_lock);
	clks[dci] = clk_register_gate(NULL, clk_output_name[dci], "dci_div1",
			CLK_SET_RATE_PARENT, SLCR_DCI_CLK_CTRL, 0, 0,
			&dciclk_lock);
	clk_prepare_enable(clks[dci]);

	/* Peripheral clocks */
	for (i = fclk0; i <= fclk3; i++)
		zynq_clk_register_fclk(i, clk_output_name[i],
				SLCR_FPGA0_CLK_CTRL + 0x10 * (i - fclk0),
				periph_parents);

	zynq_clk_register_periph_clk(lqspi, 0, clk_output_name[lqspi], NULL,
			SLCR_LQSPI_CLK_CTRL, periph_parents, 0);

	zynq_clk_register_periph_clk(smc, 0, clk_output_name[smc], NULL,
			SLCR_SMC_CLK_CTRL, periph_parents, 0);

	zynq_clk_register_periph_clk(pcap, 0, clk_output_name[pcap], NULL,
			SLCR_PCAP_CLK_CTRL, periph_parents, 0);

	zynq_clk_register_periph_clk(sdio0, sdio1, clk_output_name[sdio0],
			clk_output_name[sdio1], SLCR_SDIO_CLK_CTRL,
			periph_parents, 1);

	zynq_clk_register_periph_clk(uart0, uart1, clk_output_name[uart0],
			clk_output_name[uart1], SLCR_UART_CLK_CTRL,
			periph_parents, 1);

	zynq_clk_register_periph_clk(spi0, spi1, clk_output_name[spi0],
			clk_output_name[spi1], SLCR_SPI_CLK_CTRL,
			periph_parents, 1);

	for (i = 0; i < ARRAY_SIZE(gem0_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem0_emio_input_names[i]);
		if (idx >= 0)
			gem0_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}
	clk = clk_register_mux(NULL, "gem0_mux", periph_parents, 4, 0,
			SLCR_GEM0_CLK_CTRL, 4, 2, 0, &gem0clk_lock);
	clk = clk_register_divider(NULL, "gem0_div0", "gem0_mux", 0,
			SLCR_GEM0_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &gem0clk_lock);
	clk = clk_register_divider(NULL, "gem0_div1", "gem0_div0",
			CLK_SET_RATE_PARENT, SLCR_GEM0_CLK_CTRL, 20, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&gem0clk_lock);
	clk = clk_register_mux(NULL, "gem0_emio_mux", gem0_mux_parents, 2, 0,
			SLCR_GEM0_CLK_CTRL, 6, 1, 0, &gem0clk_lock);
	clks[gem0] = clk_register_gate(NULL, clk_output_name[gem0],
			"gem0_emio_mux", CLK_SET_RATE_PARENT,
			SLCR_GEM0_CLK_CTRL, 0, 0, &gem0clk_lock);

	for (i = 0; i < ARRAY_SIZE(gem1_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				gem1_emio_input_names[i]);
		if (idx >= 0)
			gem1_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}
	clk = clk_register_mux(NULL, "gem1_mux", periph_parents, 4, 0,
			SLCR_GEM1_CLK_CTRL, 4, 2, 0, &gem1clk_lock);
	clk = clk_register_divider(NULL, "gem1_div0", "gem1_mux", 0,
			SLCR_GEM1_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &gem1clk_lock);
	clk = clk_register_divider(NULL, "gem1_div1", "gem1_div0",
			CLK_SET_RATE_PARENT, SLCR_GEM1_CLK_CTRL, 20, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&gem1clk_lock);
	clk = clk_register_mux(NULL, "gem1_emio_mux", gem1_mux_parents, 2, 0,
			SLCR_GEM1_CLK_CTRL, 6, 1, 0, &gem1clk_lock);
	clks[gem1] = clk_register_gate(NULL, clk_output_name[gem1],
			"gem1_emio_mux", CLK_SET_RATE_PARENT,
			SLCR_GEM1_CLK_CTRL, 0, 0, &gem1clk_lock);

	tmp = strlen("mio_clk_00x");
	clk_name = kmalloc(tmp, GFP_KERNEL);
	for (i = 0; i < NUM_MIO_PINS; i++) {
		int idx;

		snprintf(clk_name, tmp, "mio_clk_%2.2d", i);
		idx = of_property_match_string(np, "clock-names", clk_name);
		if (idx >= 0)
			can_mio_mux_parents[i] = of_clk_get_parent_name(np,
						idx);
		else
			can_mio_mux_parents[i] = dummy_nm;
	}
	kfree(clk_name);
	clk = clk_register_mux(NULL, "can_mux", periph_parents, 4, 0,
			SLCR_CAN_CLK_CTRL, 4, 2, 0, &canclk_lock);
	clk = clk_register_divider(NULL, "can_div0", "can_mux", 0,
			SLCR_CAN_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &canclk_lock);
	clk = clk_register_divider(NULL, "can_div1", "can_div0",
			CLK_SET_RATE_PARENT, SLCR_CAN_CLK_CTRL, 20, 6,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&canclk_lock);
	clk = clk_register_gate(NULL, "can0_gate", "can_div1",
			CLK_SET_RATE_PARENT, SLCR_CAN_CLK_CTRL, 0, 0,
			&canclk_lock);
	clk = clk_register_gate(NULL, "can1_gate", "can_div1",
			CLK_SET_RATE_PARENT, SLCR_CAN_CLK_CTRL, 1, 0,
			&canclk_lock);
	clk = clk_register_mux(NULL, "can0_mio_mux",
			can_mio_mux_parents, 54, CLK_SET_RATE_PARENT,
			SLCR_CAN_MIOCLK_CTRL, 0, 6, 0, &canmioclk_lock);
	clk = clk_register_mux(NULL, "can1_mio_mux",
			can_mio_mux_parents, 54, CLK_SET_RATE_PARENT,
			SLCR_CAN_MIOCLK_CTRL, 16, 6, 0, &canmioclk_lock);
	clks[can0] = clk_register_mux(NULL, clk_output_name[can0],
			can0_mio_mux2_parents, 2, CLK_SET_RATE_PARENT,
			SLCR_CAN_MIOCLK_CTRL, 6, 1, 0, &canmioclk_lock);
	clks[can1] = clk_register_mux(NULL, clk_output_name[can1],
			can1_mio_mux2_parents, 2, CLK_SET_RATE_PARENT,
			SLCR_CAN_MIOCLK_CTRL, 22, 1, 0, &canmioclk_lock);

	for (i = 0; i < ARRAY_SIZE(dbgtrc_emio_input_names); i++) {
		int idx = of_property_match_string(np, "clock-names",
				dbgtrc_emio_input_names[i]);
		if (idx >= 0)
			dbg_emio_mux_parents[i + 1] = of_clk_get_parent_name(np,
					idx);
	}
	clk = clk_register_mux(NULL, "dbg_mux", periph_parents, 4, 0,
			SLCR_DBG_CLK_CTRL, 4, 2, 0, &dbgclk_lock);
	clk = clk_register_divider(NULL, "dbg_div", "dbg_mux", 0,
			SLCR_DBG_CLK_CTRL, 8, 6, CLK_DIVIDER_ONE_BASED |
			CLK_DIVIDER_ALLOW_ZERO, &dbgclk_lock);
	clk = clk_register_mux(NULL, "dbg_emio_mux", dbg_emio_mux_parents, 2, 0,
			SLCR_DBG_CLK_CTRL, 6, 1, 0, &dbgclk_lock);
	clks[dbg_trc] = clk_register_gate(NULL, clk_output_name[dbg_trc],
			"dbg_emio_mux", CLK_SET_RATE_PARENT, SLCR_DBG_CLK_CTRL,
			0, 0, &dbgclk_lock);
	clks[dbg_apb] = clk_register_gate(NULL, clk_output_name[dbg_apb],
			clk_output_name[cpu_1x], 0, SLCR_DBG_CLK_CTRL, 1, 0,
			&dbgclk_lock);

	/* One gated clock for all APER clocks. */
	clks[dma] = clk_register_gate(NULL, clk_output_name[dma],
			clk_output_name[cpu_2x], 0, SLCR_APER_CLK_CTRL, 0, 0,
			&aperclk_lock);
	clks[usb0_aper] = clk_register_gate(NULL, clk_output_name[usb0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 2, 0,
			&aperclk_lock);
	clks[usb1_aper] = clk_register_gate(NULL, clk_output_name[usb1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 3, 0,
			&aperclk_lock);
	clks[gem0_aper] = clk_register_gate(NULL, clk_output_name[gem0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 6, 0,
			&aperclk_lock);
	clks[gem1_aper] = clk_register_gate(NULL, clk_output_name[gem1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 7, 0,
			&aperclk_lock);
	clks[sdio0_aper] = clk_register_gate(NULL, clk_output_name[sdio0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 10, 0,
			&aperclk_lock);
	clks[sdio1_aper] = clk_register_gate(NULL, clk_output_name[sdio1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 11, 0,
			&aperclk_lock);
	clks[spi0_aper] = clk_register_gate(NULL, clk_output_name[spi0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 14, 0,
			&aperclk_lock);
	clks[spi1_aper] = clk_register_gate(NULL, clk_output_name[spi1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 15, 0,
			&aperclk_lock);
	clks[can0_aper] = clk_register_gate(NULL, clk_output_name[can0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 16, 0,
			&aperclk_lock);
	clks[can1_aper] = clk_register_gate(NULL, clk_output_name[can1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 17, 0,
			&aperclk_lock);
	clks[i2c0_aper] = clk_register_gate(NULL, clk_output_name[i2c0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 18, 0,
			&aperclk_lock);
	clks[i2c1_aper] = clk_register_gate(NULL, clk_output_name[i2c1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 19, 0,
			&aperclk_lock);
	clks[uart0_aper] = clk_register_gate(NULL, clk_output_name[uart0_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 20, 0,
			&aperclk_lock);
	clks[uart1_aper] = clk_register_gate(NULL, clk_output_name[uart1_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 21, 0,
			&aperclk_lock);
	clks[gpio_aper] = clk_register_gate(NULL, clk_output_name[gpio_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 22, 0,
			&aperclk_lock);
	clks[lqspi_aper] = clk_register_gate(NULL, clk_output_name[lqspi_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 23, 0,
			&aperclk_lock);
	clks[smc_aper] = clk_register_gate(NULL, clk_output_name[smc_aper],
			clk_output_name[cpu_1x], 0, SLCR_APER_CLK_CTRL, 24, 0,
			&aperclk_lock);

	for (i = 0; i < ARRAY_SIZE(clks); i++) {
		if (IS_ERR(clks[i])) {
			pr_err("Zynq clk %d: register failed with %ld\n",
			       i, PTR_ERR(clks[i]));
			BUG();
		}
	}

	clk_data.clks = clks;
	clk_data.clk_num = ARRAY_SIZE(clks);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

CLK_OF_DECLARE(zynq_clkc, "xlnx,ps7-clkc", zynq_clk_setup);

void __init zynq_clock_init(void __iomem *slcr_base)
{
	zynq_slcr_base_priv = slcr_base;
	of_clk_init(NULL);
}
