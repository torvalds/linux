// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Sam Shih <sam.shih@mediatek.com>
 * Author: Wenzhen Yu <wenzhen.yu@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include "clk-mtk.h"
#include "clk-gate.h"
#include "clk-mux.h"

#include <dt-bindings/clock/mt7986-clk.h>
#include <linux/clk.h>

static DEFINE_SPINLOCK(mt7986_clk_lock);

static const struct mtk_fixed_factor infra_divs[] = {
	FACTOR(CLK_INFRA_SYSAXI_D2, "infra_sysaxi_d2", "sysaxi_sel", 1, 2),
};

static const char *const infra_uart_parent[] __initconst = { "csw_f26m_sel",
							     "uart_sel" };

static const char *const infra_spi_parents[] __initconst = { "i2c_sel",
							     "spi_sel" };

static const char *const infra_pwm_bsel_parents[] __initconst = {
	"top_rtc_32p7k", "csw_f26m_sel", "infra_sysaxi_d2", "pwm_sel"
};

static const char *const infra_pcie_parents[] __initconst = {
	"top_rtc_32p7k", "csw_f26m_sel", "top_xtal", "pextp_tl_ck_sel"
};

static const struct mtk_mux infra_muxes[] = {
	/* MODULE_CLK_SEL_0 */
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_UART0_SEL, "infra_uart0_sel",
			     infra_uart_parent, 0x0018, 0x0010, 0x0014, 0, 1,
			     -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_UART1_SEL, "infra_uart1_sel",
			     infra_uart_parent, 0x0018, 0x0010, 0x0014, 1, 1,
			     -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_UART2_SEL, "infra_uart2_sel",
			     infra_uart_parent, 0x0018, 0x0010, 0x0014, 2, 1,
			     -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_SPI0_SEL, "infra_spi0_sel",
			     infra_spi_parents, 0x0018, 0x0010, 0x0014, 4, 1,
			     -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_SPI1_SEL, "infra_spi1_sel",
			     infra_spi_parents, 0x0018, 0x0010, 0x0014, 5, 1,
			     -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_PWM1_SEL, "infra_pwm1_sel",
			     infra_pwm_bsel_parents, 0x0018, 0x0010, 0x0014, 9,
			     2, -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_PWM2_SEL, "infra_pwm2_sel",
			     infra_pwm_bsel_parents, 0x0018, 0x0010, 0x0014, 11,
			     2, -1, -1, -1),
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_PWM_BSEL, "infra_pwm_bsel",
			     infra_pwm_bsel_parents, 0x0018, 0x0010, 0x0014, 13,
			     2, -1, -1, -1),
	/* MODULE_CLK_SEL_1 */
	MUX_GATE_CLR_SET_UPD(CLK_INFRA_PCIE_SEL, "infra_pcie_sel",
			     infra_pcie_parents, 0x0028, 0x0020, 0x0024, 0, 2,
			     -1, -1, -1),
};

static const struct mtk_gate_regs infra0_cg_regs = {
	.set_ofs = 0x40,
	.clr_ofs = 0x44,
	.sta_ofs = 0x48,
};

static const struct mtk_gate_regs infra1_cg_regs = {
	.set_ofs = 0x50,
	.clr_ofs = 0x54,
	.sta_ofs = 0x58,
};

static const struct mtk_gate_regs infra2_cg_regs = {
	.set_ofs = 0x60,
	.clr_ofs = 0x64,
	.sta_ofs = 0x68,
};

#define GATE_INFRA0(_id, _name, _parent, _shift)                               \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &infra0_cg_regs, .shift = _shift,                      \
		.ops = &mtk_clk_gate_ops_setclr,                               \
	}

#define GATE_INFRA1(_id, _name, _parent, _shift)                               \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &infra1_cg_regs, .shift = _shift,                      \
		.ops = &mtk_clk_gate_ops_setclr,                               \
	}

#define GATE_INFRA2(_id, _name, _parent, _shift)                               \
	{                                                                      \
		.id = _id, .name = _name, .parent_name = _parent,              \
		.regs = &infra2_cg_regs, .shift = _shift,                      \
		.ops = &mtk_clk_gate_ops_setclr,                               \
	}

static const struct mtk_gate infra_clks[] = {
	/* INFRA0 */
	GATE_INFRA0(CLK_INFRA_GPT_STA, "infra_gpt_sta", "infra_sysaxi_d2", 0),
	GATE_INFRA0(CLK_INFRA_PWM_HCK, "infra_pwm_hck", "infra_sysaxi_d2", 1),
	GATE_INFRA0(CLK_INFRA_PWM_STA, "infra_pwm_sta", "infra_pwm_bsel", 2),
	GATE_INFRA0(CLK_INFRA_PWM1_CK, "infra_pwm1", "infra_pwm1_sel", 3),
	GATE_INFRA0(CLK_INFRA_PWM2_CK, "infra_pwm2", "infra_pwm2_sel", 4),
	GATE_INFRA0(CLK_INFRA_CQ_DMA_CK, "infra_cq_dma", "sysaxi_sel", 6),
	GATE_INFRA0(CLK_INFRA_EIP97_CK, "infra_eip97", "eip_b_sel", 7),
	GATE_INFRA0(CLK_INFRA_AUD_BUS_CK, "infra_aud_bus", "sysaxi_sel", 8),
	GATE_INFRA0(CLK_INFRA_AUD_26M_CK, "infra_aud_26m", "csw_f26m_sel", 9),
	GATE_INFRA0(CLK_INFRA_AUD_L_CK, "infra_aud_l", "aud_l_sel", 10),
	GATE_INFRA0(CLK_INFRA_AUD_AUD_CK, "infra_aud_aud", "a1sys_sel", 11),
	GATE_INFRA0(CLK_INFRA_AUD_EG2_CK, "infra_aud_eg2", "a_tuner_sel", 13),
	GATE_INFRA0(CLK_INFRA_DRAMC_26M_CK, "infra_dramc_26m", "csw_f26m_sel",
		    14),
	GATE_INFRA0(CLK_INFRA_DBG_CK, "infra_dbg", "infra_sysaxi_d2", 15),
	GATE_INFRA0(CLK_INFRA_AP_DMA_CK, "infra_ap_dma", "infra_sysaxi_d2", 16),
	GATE_INFRA0(CLK_INFRA_SEJ_CK, "infra_sej", "infra_sysaxi_d2", 24),
	GATE_INFRA0(CLK_INFRA_SEJ_13M_CK, "infra_sej_13m", "csw_f26m_sel", 25),
	GATE_INFRA0(CLK_INFRA_TRNG_CK, "infra_trng", "sysaxi_sel", 26),
	/* INFRA1 */
	GATE_INFRA1(CLK_INFRA_THERM_CK, "infra_therm", "csw_f26m_sel", 0),
	GATE_INFRA1(CLK_INFRA_I2C0_CK, "infra_i2c0", "i2c_sel", 1),
	GATE_INFRA1(CLK_INFRA_UART0_CK, "infra_uart0", "infra_uart0_sel", 2),
	GATE_INFRA1(CLK_INFRA_UART1_CK, "infra_uart1", "infra_uart1_sel", 3),
	GATE_INFRA1(CLK_INFRA_UART2_CK, "infra_uart2", "infra_uart2_sel", 4),
	GATE_INFRA1(CLK_INFRA_NFI1_CK, "infra_nfi1", "nfi1x_sel", 8),
	GATE_INFRA1(CLK_INFRA_SPINFI1_CK, "infra_spinfi1", "spinfi_sel", 9),
	GATE_INFRA1(CLK_INFRA_NFI_HCK_CK, "infra_nfi_hck", "infra_sysaxi_d2",
		    10),
	GATE_INFRA1(CLK_INFRA_SPI0_CK, "infra_spi0", "infra_spi0_sel", 11),
	GATE_INFRA1(CLK_INFRA_SPI1_CK, "infra_spi1", "infra_spi1_sel", 12),
	GATE_INFRA1(CLK_INFRA_SPI0_HCK_CK, "infra_spi0_hck", "infra_sysaxi_d2",
		    13),
	GATE_INFRA1(CLK_INFRA_SPI1_HCK_CK, "infra_spi1_hck", "infra_sysaxi_d2",
		    14),
	GATE_INFRA1(CLK_INFRA_FRTC_CK, "infra_frtc", "top_rtc_32k", 15),
	GATE_INFRA1(CLK_INFRA_MSDC_CK, "infra_msdc", "emmc_416m_sel", 16),
	GATE_INFRA1(CLK_INFRA_MSDC_HCK_CK, "infra_msdc_hck", "emmc_250m_sel",
		    17),
	GATE_INFRA1(CLK_INFRA_MSDC_133M_CK, "infra_msdc_133m", "sysaxi_sel",
		    18),
	GATE_INFRA1(CLK_INFRA_MSDC_66M_CK, "infra_msdc_66m", "infra_sysaxi_d2",
		    19),
	GATE_INFRA1(CLK_INFRA_ADC_26M_CK, "infra_adc_26m", "csw_f26m_sel", 20),
	GATE_INFRA1(CLK_INFRA_ADC_FRC_CK, "infra_adc_frc", "csw_f26m_sel", 21),
	GATE_INFRA1(CLK_INFRA_FBIST2FPC_CK, "infra_fbist2fpc", "nfi1x_sel", 23),
	/* INFRA2 */
	GATE_INFRA2(CLK_INFRA_IUSB_133_CK, "infra_iusb_133", "sysaxi_sel", 0),
	GATE_INFRA2(CLK_INFRA_IUSB_66M_CK, "infra_iusb_66m", "infra_sysaxi_d2",
		    1),
	GATE_INFRA2(CLK_INFRA_IUSB_SYS_CK, "infra_iusb_sys", "u2u3_sys_sel", 2),
	GATE_INFRA2(CLK_INFRA_IUSB_CK, "infra_iusb", "u2u3_sel", 3),
	GATE_INFRA2(CLK_INFRA_IPCIE_CK, "infra_ipcie", "pextp_tl_ck_sel", 12),
	GATE_INFRA2(CLK_INFRA_IPCIE_PIPE_CK, "infra_ipcie_pipe", "top_xtal",
		    13),
	GATE_INFRA2(CLK_INFRA_IPCIER_CK, "infra_ipcier", "csw_f26m_sel", 14),
	GATE_INFRA2(CLK_INFRA_IPCIEB_CK, "infra_ipcieb", "sysaxi_sel", 15),
};

static int clk_mt7986_infracfg_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;
	int r;
	void __iomem *base;
	int nr = ARRAY_SIZE(infra_divs) + ARRAY_SIZE(infra_muxes) +
		 ARRAY_SIZE(infra_clks);

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("%s(): ioremap failed\n", __func__);
		return -ENOMEM;
	}

	clk_data = mtk_alloc_clk_data(nr);

	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_factors(infra_divs, ARRAY_SIZE(infra_divs), clk_data);
	mtk_clk_register_muxes(infra_muxes, ARRAY_SIZE(infra_muxes), node,
			       &mt7986_clk_lock, clk_data);
	mtk_clk_register_gates(node, infra_clks, ARRAY_SIZE(infra_clks),
			       clk_data);

	r = of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
	if (r) {
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);
		goto free_infracfg_data;
	}
	return r;

free_infracfg_data:
	mtk_free_clk_data(clk_data);
	return r;

}

static const struct of_device_id of_match_clk_mt7986_infracfg[] = {
	{ .compatible = "mediatek,mt7986-infracfg", },
	{}
};

static struct platform_driver clk_mt7986_infracfg_drv = {
	.probe = clk_mt7986_infracfg_probe,
	.driver = {
		.name = "clk-mt7986-infracfg",
		.of_match_table = of_match_clk_mt7986_infracfg,
	},
};
builtin_platform_driver(clk_mt7986_infracfg_drv);
