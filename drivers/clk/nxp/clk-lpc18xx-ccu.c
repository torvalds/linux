/*
 * Clk driver for NXP LPC18xx/LPC43xx Clock Control Unit (CCU)
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <dt-bindings/clock/lpc18xx-ccu.h>

/* Bit defines for CCU branch configuration register */
#define LPC18XX_CCU_RUN		BIT(0)
#define LPC18XX_CCU_AUTO	BIT(1)
#define LPC18XX_CCU_DIV		BIT(5)
#define LPC18XX_CCU_DIVSTAT	BIT(27)

/* CCU branch feature bits */
#define CCU_BRANCH_IS_BUS	BIT(0)
#define CCU_BRANCH_HAVE_DIV2	BIT(1)

struct lpc18xx_branch_clk_data {
	const char **name;
	int num;
};

struct lpc18xx_clk_branch {
	const char *base_name;
	const char *name;
	u16 offset;
	u16 flags;
	struct clk *clk;
	struct clk_gate gate;
};

static struct lpc18xx_clk_branch clk_branches[] = {
	{"base_apb3_clk", "apb3_bus",		CLK_APB3_BUS,		CCU_BRANCH_IS_BUS},
	{"base_apb3_clk", "apb3_i2c1",		CLK_APB3_I2C1,		0},
	{"base_apb3_clk", "apb3_dac",		CLK_APB3_DAC,		0},
	{"base_apb3_clk", "apb3_adc0",		CLK_APB3_ADC0,		0},
	{"base_apb3_clk", "apb3_adc1",		CLK_APB3_ADC1,		0},
	{"base_apb3_clk", "apb3_can0",		CLK_APB3_CAN0,		0},

	{"base_apb1_clk", "apb1_bus",		CLK_APB1_BUS,		CCU_BRANCH_IS_BUS},
	{"base_apb1_clk", "apb1_mc_pwm",	CLK_APB1_MOTOCON_PWM,	0},
	{"base_apb1_clk", "apb1_i2c0",		CLK_APB1_I2C0,		0},
	{"base_apb1_clk", "apb1_i2s",		CLK_APB1_I2S,		0},
	{"base_apb1_clk", "apb1_can1",		CLK_APB1_CAN1,		0},

	{"base_spifi_clk", "spifi",		CLK_SPIFI,		0},

	{"base_cpu_clk", "cpu_bus",		CLK_CPU_BUS,		CCU_BRANCH_IS_BUS},
	{"base_cpu_clk", "cpu_spifi",		CLK_CPU_SPIFI,		0},
	{"base_cpu_clk", "cpu_gpio",		CLK_CPU_GPIO,		0},
	{"base_cpu_clk", "cpu_lcd",		CLK_CPU_LCD,		0},
	{"base_cpu_clk", "cpu_ethernet",	CLK_CPU_ETHERNET,	0},
	{"base_cpu_clk", "cpu_usb0",		CLK_CPU_USB0,		0},
	{"base_cpu_clk", "cpu_emc",		CLK_CPU_EMC,		0},
	{"base_cpu_clk", "cpu_sdio",		CLK_CPU_SDIO,		0},
	{"base_cpu_clk", "cpu_dma",		CLK_CPU_DMA,		0},
	{"base_cpu_clk", "cpu_core",		CLK_CPU_CORE,		0},
	{"base_cpu_clk", "cpu_sct",		CLK_CPU_SCT,		0},
	{"base_cpu_clk", "cpu_usb1",		CLK_CPU_USB1,		0},
	{"base_cpu_clk", "cpu_emcdiv",		CLK_CPU_EMCDIV,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_flasha",		CLK_CPU_FLASHA,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_flashb",		CLK_CPU_FLASHB,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_m0app",		CLK_CPU_M0APP,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_adchs",		CLK_CPU_ADCHS,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_eeprom",		CLK_CPU_EEPROM,		CCU_BRANCH_HAVE_DIV2},
	{"base_cpu_clk", "cpu_wwdt",		CLK_CPU_WWDT,		0},
	{"base_cpu_clk", "cpu_uart0",		CLK_CPU_UART0,		0},
	{"base_cpu_clk", "cpu_uart1",		CLK_CPU_UART1,		0},
	{"base_cpu_clk", "cpu_ssp0",		CLK_CPU_SSP0,		0},
	{"base_cpu_clk", "cpu_timer0",		CLK_CPU_TIMER0,		0},
	{"base_cpu_clk", "cpu_timer1",		CLK_CPU_TIMER1,		0},
	{"base_cpu_clk", "cpu_scu",		CLK_CPU_SCU,		0},
	{"base_cpu_clk", "cpu_creg",		CLK_CPU_CREG,		0},
	{"base_cpu_clk", "cpu_ritimer",		CLK_CPU_RITIMER,	0},
	{"base_cpu_clk", "cpu_uart2",		CLK_CPU_UART2,		0},
	{"base_cpu_clk", "cpu_uart3",		CLK_CPU_UART3,		0},
	{"base_cpu_clk", "cpu_timer2",		CLK_CPU_TIMER2,		0},
	{"base_cpu_clk", "cpu_timer3",		CLK_CPU_TIMER3,		0},
	{"base_cpu_clk", "cpu_ssp1",		CLK_CPU_SSP1,		0},
	{"base_cpu_clk", "cpu_qei",		CLK_CPU_QEI,		0},

	{"base_periph_clk", "periph_bus",	CLK_PERIPH_BUS,		CCU_BRANCH_IS_BUS},
	{"base_periph_clk", "periph_core",	CLK_PERIPH_CORE,	0},
	{"base_periph_clk", "periph_sgpio",	CLK_PERIPH_SGPIO,	0},

	{"base_usb0_clk",  "usb0",		CLK_USB0,		0},
	{"base_usb1_clk",  "usb1",		CLK_USB1,		0},
	{"base_spi_clk",   "spi",		CLK_SPI,		0},
	{"base_adchs_clk", "adchs",		CLK_ADCHS,		0},

	{"base_audio_clk", "audio",		CLK_AUDIO,		0},
	{"base_uart3_clk", "apb2_uart3",	CLK_APB2_UART3,		0},
	{"base_uart2_clk", "apb2_uart2",	CLK_APB2_UART2,		0},
	{"base_uart1_clk", "apb0_uart1",	CLK_APB0_UART1,		0},
	{"base_uart0_clk", "apb0_uart0",	CLK_APB0_UART0,		0},
	{"base_ssp1_clk",  "apb2_ssp1",		CLK_APB2_SSP1,		0},
	{"base_ssp0_clk",  "apb0_ssp0",		CLK_APB0_SSP0,		0},
	{"base_sdio_clk",  "sdio",		CLK_SDIO,		0},
};

static struct clk *lpc18xx_ccu_branch_clk_get(struct of_phandle_args *clkspec,
					      void *data)
{
	struct lpc18xx_branch_clk_data *clk_data = data;
	unsigned int offset = clkspec->args[0];
	int i, j;

	for (i = 0; i < ARRAY_SIZE(clk_branches); i++) {
		if (clk_branches[i].offset != offset)
			continue;

		for (j = 0; j < clk_data->num; j++) {
			if (!strcmp(clk_branches[i].base_name, clk_data->name[j]))
				return clk_branches[i].clk;
		}
	}

	pr_err("%s: invalid clock offset %d\n", __func__, offset);

	return ERR_PTR(-EINVAL);
}

static int lpc18xx_ccu_gate_endisable(struct clk_hw *hw, bool enable)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 val;

	/*
	 * Divider field is write only, so divider stat field must
	 * be read so divider field can be set accordingly.
	 */
	val = readl(gate->reg);
	if (val & LPC18XX_CCU_DIVSTAT)
		val |= LPC18XX_CCU_DIV;

	if (enable) {
		val |= LPC18XX_CCU_RUN;
	} else {
		/*
		 * To safely disable a branch clock a squence of two separate
		 * writes must be used. First write should set the AUTO bit
		 * and the next write should clear the RUN bit.
		 */
		val |= LPC18XX_CCU_AUTO;
		writel(val, gate->reg);

		val &= ~LPC18XX_CCU_RUN;
	}

	writel(val, gate->reg);

	return 0;
}

static int lpc18xx_ccu_gate_enable(struct clk_hw *hw)
{
	return lpc18xx_ccu_gate_endisable(hw, true);
}

static void lpc18xx_ccu_gate_disable(struct clk_hw *hw)
{
	lpc18xx_ccu_gate_endisable(hw, false);
}

static int lpc18xx_ccu_gate_is_enabled(struct clk_hw *hw)
{
	const struct clk_hw *parent;

	/*
	 * The branch clock registers are only accessible
	 * if the base (parent) clock is enabled. Register
	 * access with a disabled base clock will hang the
	 * system.
	 */
	parent = clk_hw_get_parent(hw);
	if (!parent)
		return 0;

	if (!clk_hw_is_enabled(parent))
		return 0;

	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops lpc18xx_ccu_gate_ops = {
	.enable		= lpc18xx_ccu_gate_enable,
	.disable	= lpc18xx_ccu_gate_disable,
	.is_enabled	= lpc18xx_ccu_gate_is_enabled,
};

static void lpc18xx_ccu_register_branch_gate_div(struct lpc18xx_clk_branch *branch,
						 void __iomem *reg_base,
						 const char *parent)
{
	const struct clk_ops *div_ops = NULL;
	struct clk_divider *div = NULL;
	struct clk_hw *div_hw = NULL;

	if (branch->flags & CCU_BRANCH_HAVE_DIV2) {
		div = kzalloc(sizeof(*div), GFP_KERNEL);
		if (!div)
			return;

		div->reg = branch->offset + reg_base;
		div->flags = CLK_DIVIDER_READ_ONLY;
		div->shift = 27;
		div->width = 1;

		div_hw = &div->hw;
		div_ops = &clk_divider_ro_ops;
	}

	branch->gate.reg = branch->offset + reg_base;
	branch->gate.bit_idx = 0;

	branch->clk = clk_register_composite(NULL, branch->name, &parent, 1,
					     NULL, NULL,
					     div_hw, div_ops,
					     &branch->gate.hw, &lpc18xx_ccu_gate_ops, 0);
	if (IS_ERR(branch->clk)) {
		kfree(div);
		pr_warn("%s: failed to register %s\n", __func__, branch->name);
		return;
	}

	/* Grab essential branch clocks for CPU and SDRAM */
	switch (branch->offset) {
	case CLK_CPU_EMC:
	case CLK_CPU_CORE:
	case CLK_CPU_CREG:
	case CLK_CPU_EMCDIV:
		clk_prepare_enable(branch->clk);
	}
}

static void lpc18xx_ccu_register_branch_clks(void __iomem *reg_base,
					     const char *base_name)
{
	const char *parent = base_name;
	int i;

	for (i = 0; i < ARRAY_SIZE(clk_branches); i++) {
		if (strcmp(clk_branches[i].base_name, base_name))
			continue;

		lpc18xx_ccu_register_branch_gate_div(&clk_branches[i], reg_base,
						     parent);

		if (clk_branches[i].flags & CCU_BRANCH_IS_BUS)
			parent = clk_branches[i].name;
	}
}

static void __init lpc18xx_ccu_init(struct device_node *np)
{
	struct lpc18xx_branch_clk_data *clk_data;
	void __iomem *reg_base;
	int i, ret;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_warn("%s: failed to map address range\n", __func__);
		return;
	}

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data) {
		iounmap(reg_base);
		return;
	}

	clk_data->num = of_property_count_strings(np, "clock-names");
	clk_data->name = kcalloc(clk_data->num, sizeof(char *), GFP_KERNEL);
	if (!clk_data->name) {
		iounmap(reg_base);
		kfree(clk_data);
		return;
	}

	for (i = 0; i < clk_data->num; i++) {
		ret = of_property_read_string_index(np, "clock-names", i,
						    &clk_data->name[i]);
		if (ret) {
			pr_warn("%s: failed to get clock name at idx %d\n",
				__func__, i);
			continue;
		}

		lpc18xx_ccu_register_branch_clks(reg_base, clk_data->name[i]);
	}

	of_clk_add_provider(np, lpc18xx_ccu_branch_clk_get, clk_data);
}
CLK_OF_DECLARE(lpc18xx_ccu, "nxp,lpc1850-ccu", lpc18xx_ccu_init);
