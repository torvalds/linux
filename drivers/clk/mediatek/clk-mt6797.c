// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Kevin Chen <kevin-cw.chen@mediatek.com>
 */

#include <linux/of.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"
#include "clk-pll.h"

#include <dt-bindings/clock/mt6797-clk.h>

/*
 * For some clocks, we don't care what their actual rates are. And these
 * clocks may change their rate on different products or different scenarios.
 * So we model these clocks' rate as 0, to denote it's not an actual rate.
 */

static DEFINE_SPINLOCK(mt6797_clk_lock);

static const struct mtk_fixed_factor top_fixed_divs[] = {
	FACTOR(CLK_TOP_SYSPLL_CK, "syspll_ck", "mainpll", 1, 1),
	FACTOR(CLK_TOP_SYSPLL_D2, "syspll_d2", "mainpll", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D2, "syspll1_d2", "syspll_d2", 1, 2),
	FACTOR(CLK_TOP_SYSPLL1_D4, "syspll1_d4", "syspll_d2", 1, 4),
	FACTOR(CLK_TOP_SYSPLL1_D8, "syspll1_d8", "syspll_d2", 1, 8),
	FACTOR(CLK_TOP_SYSPLL1_D16, "syspll1_d16", "syspll_d2", 1, 16),
	FACTOR(CLK_TOP_SYSPLL_D3, "syspll_d3", "mainpll", 1, 3),
	FACTOR(CLK_TOP_SYSPLL_D3_D3, "syspll_d3_d3", "syspll_d3", 1, 3),
	FACTOR(CLK_TOP_SYSPLL2_D2, "syspll2_d2", "syspll_d3", 1, 2),
	FACTOR(CLK_TOP_SYSPLL2_D4, "syspll2_d4", "syspll_d3", 1, 4),
	FACTOR(CLK_TOP_SYSPLL2_D8, "syspll2_d8", "syspll_d3", 1, 8),
	FACTOR(CLK_TOP_SYSPLL_D5, "syspll_d5", "mainpll", 1, 5),
	FACTOR(CLK_TOP_SYSPLL3_D2, "syspll3_d2", "syspll_d5", 1, 2),
	FACTOR(CLK_TOP_SYSPLL3_D4, "syspll3_d4", "syspll_d5", 1, 4),
	FACTOR(CLK_TOP_SYSPLL_D7, "syspll_d7", "mainpll", 1, 7),
	FACTOR(CLK_TOP_SYSPLL4_D2, "syspll4_d2", "syspll_d7", 1, 2),
	FACTOR(CLK_TOP_SYSPLL4_D4, "syspll4_d4", "syspll_d7", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL_CK, "univpll_ck", "univpll", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL_D7, "univpll_d7", "univpll", 1, 7),
	FACTOR(CLK_TOP_UNIVPLL_D26, "univpll_d26", "univpll", 1, 26),
	FACTOR(CLK_TOP_SSUSB_PHY_48M_CK, "ssusb_phy_48m_ck", "univpll", 1, 1),
	FACTOR(CLK_TOP_USB_PHY48M_CK, "usb_phy48m_ck", "univpll", 1, 1),
	FACTOR(CLK_TOP_UNIVPLL_D2, "univpll_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D2, "univpll1_d2", "univpll_d2", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL1_D4, "univpll1_d4", "univpll_d2", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL1_D8, "univpll1_d8", "univpll_d2", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D3, "univpll_d3", "univpll", 1, 3),
	FACTOR(CLK_TOP_UNIVPLL2_D2, "univpll2_d2", "univpll", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL2_D4, "univpll2_d4", "univpll", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL2_D8, "univpll2_d8", "univpll", 1, 8),
	FACTOR(CLK_TOP_UNIVPLL_D5, "univpll_d5", "univpll", 1, 5),
	FACTOR(CLK_TOP_UNIVPLL3_D2, "univpll3_d2", "univpll_d5", 1, 2),
	FACTOR(CLK_TOP_UNIVPLL3_D4, "univpll3_d4", "univpll_d5", 1, 4),
	FACTOR(CLK_TOP_UNIVPLL3_D8, "univpll3_d8", "univpll_d5", 1, 8),
	FACTOR(CLK_TOP_ULPOSC_CK_ORG, "ulposc_ck_org", "ulposc", 1, 1),
	FACTOR(CLK_TOP_ULPOSC_CK, "ulposc_ck", "ulposc_ck_org", 1, 3),
	FACTOR(CLK_TOP_ULPOSC_D2, "ulposc_d2", "ulposc_ck", 1, 2),
	FACTOR(CLK_TOP_ULPOSC_D3, "ulposc_d3", "ulposc_ck", 1, 4),
	FACTOR(CLK_TOP_ULPOSC_D4, "ulposc_d4", "ulposc_ck", 1, 8),
	FACTOR(CLK_TOP_ULPOSC_D8, "ulposc_d8", "ulposc_ck", 1, 10),
	FACTOR(CLK_TOP_ULPOSC_D10, "ulposc_d10", "ulposc_ck_org", 1, 1),
	FACTOR(CLK_TOP_APLL1_CK, "apll1_ck", "apll1", 1, 1),
	FACTOR(CLK_TOP_APLL2_CK, "apll2_ck", "apll2", 1, 1),
	FACTOR(CLK_TOP_MFGPLL_CK, "mfgpll_ck", "mfgpll", 1, 1),
	FACTOR(CLK_TOP_MFGPLL_D2, "mfgpll_d2", "mfgpll_ck", 1, 2),
	FACTOR(CLK_TOP_IMGPLL_CK, "imgpll_ck", "imgpll", 1, 1),
	FACTOR(CLK_TOP_IMGPLL_D2, "imgpll_d2", "imgpll_ck", 1, 2),
	FACTOR(CLK_TOP_IMGPLL_D4, "imgpll_d4", "imgpll_ck", 1, 4),
	FACTOR(CLK_TOP_CODECPLL_CK, "codecpll_ck", "codecpll", 1, 1),
	FACTOR(CLK_TOP_CODECPLL_D2, "codecpll_d2", "codecpll_ck", 1, 2),
	FACTOR(CLK_TOP_VDECPLL_CK, "vdecpll_ck", "vdecpll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_CK, "tvdpll_ck", "tvdpll", 1, 1),
	FACTOR(CLK_TOP_TVDPLL_D2, "tvdpll_d2", "tvdpll_ck", 1, 2),
	FACTOR(CLK_TOP_TVDPLL_D4, "tvdpll_d4", "tvdpll_ck", 1, 4),
	FACTOR(CLK_TOP_TVDPLL_D8, "tvdpll_d8", "tvdpll_ck", 1, 8),
	FACTOR(CLK_TOP_TVDPLL_D16, "tvdpll_d16", "tvdpll_ck", 1, 16),
	FACTOR(CLK_TOP_MSDCPLL_CK, "msdcpll_ck", "msdcpll", 1, 1),
	FACTOR(CLK_TOP_MSDCPLL_D2, "msdcpll_d2", "msdcpll_ck", 1, 2),
	FACTOR(CLK_TOP_MSDCPLL_D4, "msdcpll_d4", "msdcpll_ck", 1, 4),
	FACTOR(CLK_TOP_MSDCPLL_D8, "msdcpll_d8", "msdcpll_ck", 1, 8),
};

static const char * const axi_parents[] = {
	"clk26m",
	"syspll_d7",
	"ulposc_axi_ck_mux",
};

static const char * const ulposc_axi_ck_mux_parents[] = {
	"syspll1_d4",
	"ulposc_axi_ck_mux_pre",
};

static const char * const ulposc_axi_ck_mux_pre_parents[] = {
	"ulposc_d2",
	"ulposc_d3",
};

static const char * const ddrphycfg_parents[] = {
	"clk26m",
	"syspll3_d2",
	"syspll2_d4",
	"syspll1_d8",
};

static const char * const mm_parents[] = {
	"clk26m",
	"imgpll_ck",
	"univpll1_d2",
	"syspll1_d2",
};

static const char * const pwm_parents[] = {
	"clk26m",
	"univpll2_d4",
	"ulposc_d2",
	"ulposc_d3",
	"ulposc_d8",
	"ulposc_d10",
	"ulposc_d4",
};

static const char * const vdec_parents[] = {
	"clk26m",
	"vdecpll_ck",
	"imgpll_ck",
	"syspll_d3",
	"univpll_d5",
	"clk26m",
	"clk26m",
};

static const char * const venc_parents[] = {
	"clk26m",
	"codecpll_ck",
	"syspll_d3",
};

static const char * const mfg_parents[] = {
	"clk26m",
	"mfgpll_ck",
	"syspll_d3",
	"univpll_d3",
};

static const char * const camtg[] = {
	"clk26m",
	"univpll_d26",
	"univpll2_d2",
};

static const char * const uart_parents[] = {
	"clk26m",
	"univpll2_d8",
};

static const char * const spi_parents[] = {
	"clk26m",
	"syspll3_d2",
	"syspll2_d4",
	"ulposc_spi_ck_mux",
};

static const char * const ulposc_spi_ck_mux_parents[] = {
	"ulposc_d2",
	"ulposc_d3",
};

static const char * const usb20_parents[] = {
	"clk26m",
	"univpll1_d8",
	"syspll4_d2",
};

static const char * const msdc50_0_hclk_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll2_d2",
	"syspll4_d2",
};

static const char * const msdc50_0_parents[] = {
	"clk26m",
	"msdcpll",
	"syspll_d3",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"msdcpll_d2",
	"univpll1_d2",
	"univpll_d3",
};

static const char * const msdc30_1_parents[] = {
	"clk26m",
	"univpll2_d2",
	"msdcpll_d2",
	"univpll1_d4",
	"syspll2_d2",
	"syspll_d7",
	"univpll_d7",
};

static const char * const msdc30_2_parents[] = {
	"clk26m",
	"univpll2_d8",
	"syspll2_d8",
	"syspll1_d8",
	"msdcpll_d8",
	"syspll3_d4",
	"univpll_d26",
};

static const char * const audio_parents[] = {
	"clk26m",
	"syspll3_d4",
	"syspll4_d4",
	"syspll1_d16",
};

static const char * const aud_intbus_parents[] = {
	"clk26m",
	"syspll1_d4",
	"syspll4_d2",
};

static const char * const pmicspi_parents[] = {
	"clk26m",
	"univpll_d26",
	"syspll3_d4",
	"syspll1_d8",
	"ulposc_d4",
	"ulposc_d8",
	"syspll2_d8",
};

static const char * const scp_parents[] = {
	"clk26m",
	"syspll_d3",
	"ulposc_ck",
	"univpll_d5",
};

static const char * const atb_parents[] = {
	"clk26m",
	"syspll1_d2",
	"syspll_d5",
};

static const char * const mjc_parents[] = {
	"clk26m",
	"imgpll_ck",
	"univpll_d5",
	"syspll1_d2",
};

static const char * const dpi0_parents[] = {
	"clk26m",
	"tvdpll_d2",
	"tvdpll_d4",
	"tvdpll_d8",
	"tvdpll_d16",
	"clk26m",
	"clk26m",
};

static const char * const aud_1_parents[] = {
	"clk26m",
	"apll1_ck",
};

static const char * const aud_2_parents[] = {
	"clk26m",
	"apll2_ck",
};

static const char * const ssusb_top_sys_parents[] = {
	"clk26m",
	"univpll3_d2",
};

static const char * const spm_parents[] = {
	"clk26m",
	"syspll1_d8",
};

static const char * const bsi_spi_parents[] = {
	"clk26m",
	"syspll_d3_d3",
	"syspll1_d4",
	"syspll_d7",
};

static const char * const audio_h_parents[] = {
	"clk26m",
	"apll2_ck",
	"apll1_ck",
	"univpll_d7",
};

static const char * const mfg_52m_parents[] = {
	"clk26m",
	"univpll2_d8",
	"univpll2_d4",
	"univpll2_d4",
};

static const char * const anc_md32_parents[] = {
	"clk26m",
	"syspll1_d2",
	"univpll_d5",
};

/*
 * Clock mux ddrphycfg is needed by the DRAM controller. We mark it as
 * critical as otherwise the system will hang after boot.
 */
static const struct mtk_composite top_muxes[] = {
	MUX(CLK_TOP_MUX_ULPOSC_AXI_CK_MUX_PRE, "ulposc_axi_ck_mux_pre",
	    ulposc_axi_ck_mux_pre_parents, 0x0040, 3, 1),
	MUX(CLK_TOP_MUX_ULPOSC_AXI_CK_MUX, "ulposc_axi_ck_mux",
	    ulposc_axi_ck_mux_parents, 0x0040, 2, 1),
	MUX(CLK_TOP_MUX_AXI, "axi_sel", axi_parents,
	    0x0040, 0, 2),
	MUX_FLAGS(CLK_TOP_MUX_DDRPHYCFG, "ddrphycfg_sel", ddrphycfg_parents,
		  0x0040, 16, 2, CLK_IS_CRITICAL | CLK_SET_RATE_PARENT),
	MUX(CLK_TOP_MUX_MM, "mm_sel", mm_parents,
	    0x0040, 24, 2),
	MUX_GATE(CLK_TOP_MUX_PWM, "pwm_sel", pwm_parents, 0x0050, 0, 3, 7),
	MUX_GATE(CLK_TOP_MUX_VDEC, "vdec_sel", vdec_parents, 0x0050, 8, 3, 15),
	MUX_GATE(CLK_TOP_MUX_VENC, "venc_sel", venc_parents, 0x0050, 16, 2, 23),
	MUX_GATE(CLK_TOP_MUX_MFG, "mfg_sel", mfg_parents, 0x0050, 24, 2, 31),
	MUX_GATE(CLK_TOP_MUX_CAMTG, "camtg_sel", camtg, 0x0060, 0, 2, 7),
	MUX_GATE(CLK_TOP_MUX_UART, "uart_sel", uart_parents, 0x0060, 8, 1, 15),
	MUX_GATE(CLK_TOP_MUX_SPI, "spi_sel", spi_parents, 0x0060, 16, 2, 23),
	MUX(CLK_TOP_MUX_ULPOSC_SPI_CK_MUX, "ulposc_spi_ck_mux",
	    ulposc_spi_ck_mux_parents, 0x0060, 18, 1),
	MUX_GATE(CLK_TOP_MUX_USB20, "usb20_sel", usb20_parents,
		 0x0060, 24, 2, 31),
	MUX(CLK_TOP_MUX_MSDC50_0_HCLK, "msdc50_0_hclk_sel",
	    msdc50_0_hclk_parents, 0x0070, 8, 2),
	MUX_GATE(CLK_TOP_MUX_MSDC50_0, "msdc50_0_sel", msdc50_0_parents,
		 0x0070, 16, 4, 23),
	MUX_GATE(CLK_TOP_MUX_MSDC30_1, "msdc30_1_sel", msdc30_1_parents,
		 0x0070, 24, 3, 31),
	MUX_GATE(CLK_TOP_MUX_MSDC30_2, "msdc30_2_sel", msdc30_2_parents,
		 0x0080, 0, 3, 7),
	MUX_GATE(CLK_TOP_MUX_AUDIO, "audio_sel", audio_parents,
		 0x0080, 16, 2, 23),
	MUX(CLK_TOP_MUX_AUD_INTBUS, "aud_intbus_sel", aud_intbus_parents,
	    0x0080, 24, 2),
	MUX(CLK_TOP_MUX_PMICSPI, "pmicspi_sel", pmicspi_parents,
	    0x0090, 0, 3),
	MUX(CLK_TOP_MUX_SCP, "scp_sel", scp_parents,
	    0x0090, 8, 2),
	MUX(CLK_TOP_MUX_ATB, "atb_sel", atb_parents,
	    0x0090, 16, 2),
	MUX_GATE(CLK_TOP_MUX_MJC, "mjc_sel", mjc_parents, 0x0090, 24, 2, 31),
	MUX_GATE(CLK_TOP_MUX_DPI0, "dpi0_sel", dpi0_parents, 0x00A0, 0, 3, 7),
	MUX_GATE(CLK_TOP_MUX_AUD_1, "aud_1_sel", aud_1_parents,
		 0x00A0, 16, 1, 23),
	MUX_GATE(CLK_TOP_MUX_AUD_2, "aud_2_sel", aud_2_parents,
		 0x00A0, 24, 1, 31),
	MUX(CLK_TOP_MUX_SSUSB_TOP_SYS, "ssusb_top_sys_sel",
	    ssusb_top_sys_parents, 0x00B0, 8, 1),
	MUX(CLK_TOP_MUX_SPM, "spm_sel", spm_parents,
	    0x00C0, 0, 1),
	MUX(CLK_TOP_MUX_BSI_SPI, "bsi_spi_sel", bsi_spi_parents,
	    0x00C0, 8, 2),
	MUX_GATE(CLK_TOP_MUX_AUDIO_H, "audio_h_sel", audio_h_parents,
		 0x00C0, 16, 2, 23),
	MUX_GATE(CLK_TOP_MUX_ANC_MD32, "anc_md32_sel", anc_md32_parents,
		 0x00C0, 24, 2, 31),
	MUX(CLK_TOP_MUX_MFG_52M, "mfg_52m_sel", mfg_52m_parents,
	    0x0104, 1, 2),
};

static int mtk_topckgen_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	void __iomem *base;
	struct device_node *node = pdev->dev.of_node;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	clk_data = mtk_alloc_clk_data(CLK_TOP_NR);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_factors(top_fixed_divs, ARRAY_SIZE(top_fixed_divs),
				 clk_data);

	mtk_clk_register_composites(&pdev->dev, top_muxes,
				    ARRAY_SIZE(top_muxes), base,
				    &mt6797_clk_lock, clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static const struct mtk_gate_regs infra0_cg_regs = {
	.set_ofs = 0x0080,
	.clr_ofs = 0x0084,
	.sta_ofs = 0x0090,
};

static const struct mtk_gate_regs infra1_cg_regs = {
	.set_ofs = 0x0088,
	.clr_ofs = 0x008c,
	.sta_ofs = 0x0094,
};

static const struct mtk_gate_regs infra2_cg_regs = {
	.set_ofs = 0x00a8,
	.clr_ofs = 0x00ac,
	.sta_ofs = 0x00b0,
};

#define GATE_ICG0(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra0_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_ICG1(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra1_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_ICG1_FLAGS(_id, _name, _parent, _shift, _flags)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &infra1_cg_regs, _shift,	\
		       &mtk_clk_gate_ops_setclr, _flags)

#define GATE_ICG2(_id, _name, _parent, _shift)				\
	GATE_MTK(_id, _name, _parent, &infra2_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

#define GATE_ICG2_FLAGS(_id, _name, _parent, _shift, _flags)		\
	GATE_MTK_FLAGS(_id, _name, _parent, &infra2_cg_regs, _shift,	\
		       &mtk_clk_gate_ops_setclr, _flags)

/*
 * Clock gates dramc and dramc_b are needed by the DRAM controller.
 * We mark them as critical as otherwise the system will hang after boot.
 */
static const struct mtk_gate infra_clks[] = {
	GATE_ICG0(CLK_INFRA_PMIC_TMR, "infra_pmic_tmr", "ulposc", 0),
	GATE_ICG0(CLK_INFRA_PMIC_AP, "infra_pmic_ap", "pmicspi_sel", 1),
	GATE_ICG0(CLK_INFRA_PMIC_MD, "infra_pmic_md", "pmicspi_sel", 2),
	GATE_ICG0(CLK_INFRA_PMIC_CONN, "infra_pmic_conn", "pmicspi_sel", 3),
	GATE_ICG0(CLK_INFRA_SCP, "infra_scp", "scp_sel", 4),
	GATE_ICG0(CLK_INFRA_SEJ, "infra_sej", "axi_sel", 5),
	GATE_ICG0(CLK_INFRA_APXGPT, "infra_apxgpt", "axi_sel", 6),
	GATE_ICG0(CLK_INFRA_SEJ_13M, "infra_sej_13m", "clk26m", 7),
	GATE_ICG0(CLK_INFRA_ICUSB, "infra_icusb", "usb20_sel", 8),
	GATE_ICG0(CLK_INFRA_GCE, "infra_gce", "axi_sel", 9),
	GATE_ICG0(CLK_INFRA_THERM, "infra_therm", "axi_sel", 10),
	GATE_ICG0(CLK_INFRA_I2C0, "infra_i2c0", "axi_sel", 11),
	GATE_ICG0(CLK_INFRA_I2C1, "infra_i2c1", "axi_sel", 12),
	GATE_ICG0(CLK_INFRA_I2C2, "infra_i2c2", "axi_sel", 13),
	GATE_ICG0(CLK_INFRA_I2C3, "infra_i2c3", "axi_sel", 14),
	GATE_ICG0(CLK_INFRA_PWM_HCLK, "infra_pwm_hclk", "axi_sel", 15),
	GATE_ICG0(CLK_INFRA_PWM1, "infra_pwm1", "axi_sel", 16),
	GATE_ICG0(CLK_INFRA_PWM2, "infra_pwm2", "axi_sel", 17),
	GATE_ICG0(CLK_INFRA_PWM3, "infra_pwm3", "axi_sel", 18),
	GATE_ICG0(CLK_INFRA_PWM4, "infra_pwm4", "axi_sel", 19),
	GATE_ICG0(CLK_INFRA_PWM, "infra_pwm", "axi_sel", 21),
	GATE_ICG0(CLK_INFRA_UART0, "infra_uart0", "uart_sel", 22),
	GATE_ICG0(CLK_INFRA_UART1, "infra_uart1", "uart_sel", 23),
	GATE_ICG0(CLK_INFRA_UART2, "infra_uart2", "uart_sel", 24),
	GATE_ICG0(CLK_INFRA_UART3, "infra_uart3", "uart_sel", 25),
	GATE_ICG0(CLK_INFRA_MD2MD_CCIF_0, "infra_md2md_ccif_0", "axi_sel", 27),
	GATE_ICG0(CLK_INFRA_MD2MD_CCIF_1, "infra_md2md_ccif_1", "axi_sel", 28),
	GATE_ICG0(CLK_INFRA_MD2MD_CCIF_2, "infra_md2md_ccif_2", "axi_sel", 29),
	GATE_ICG0(CLK_INFRA_FHCTL, "infra_fhctl", "clk26m", 30),
	GATE_ICG0(CLK_INFRA_BTIF, "infra_btif", "axi_sel", 31),
	GATE_ICG1(CLK_INFRA_MD2MD_CCIF_3, "infra_md2md_ccif_3", "axi_sel", 0),
	GATE_ICG1(CLK_INFRA_SPI, "infra_spi", "spi_sel", 1),
	GATE_ICG1(CLK_INFRA_MSDC0, "infra_msdc0", "msdc50_0_sel", 2),
	GATE_ICG1(CLK_INFRA_MD2MD_CCIF_4, "infra_md2md_ccif_4", "axi_sel", 3),
	GATE_ICG1(CLK_INFRA_MSDC1, "infra_msdc1", "msdc30_1_sel", 4),
	GATE_ICG1(CLK_INFRA_MSDC2, "infra_msdc2", "msdc30_2_sel", 5),
	GATE_ICG1(CLK_INFRA_MD2MD_CCIF_5, "infra_md2md_ccif_5", "axi_sel", 7),
	GATE_ICG1(CLK_INFRA_GCPU, "infra_gcpu", "axi_sel", 8),
	GATE_ICG1(CLK_INFRA_TRNG, "infra_trng", "axi_sel", 9),
	GATE_ICG1(CLK_INFRA_AUXADC, "infra_auxadc", "clk26m", 10),
	GATE_ICG1(CLK_INFRA_CPUM, "infra_cpum", "axi_sel", 11),
	GATE_ICG1(CLK_INFRA_AP_C2K_CCIF_0, "infra_ap_c2k_ccif_0",
		  "axi_sel", 12),
	GATE_ICG1(CLK_INFRA_AP_C2K_CCIF_1, "infra_ap_c2k_ccif_1",
		  "axi_sel", 13),
	GATE_ICG1(CLK_INFRA_CLDMA, "infra_cldma", "axi_sel", 16),
	GATE_ICG1(CLK_INFRA_DISP_PWM, "infra_disp_pwm", "pwm_sel", 17),
	GATE_ICG1(CLK_INFRA_AP_DMA, "infra_ap_dma", "axi_sel", 18),
	GATE_ICG1(CLK_INFRA_DEVICE_APC, "infra_device_apc", "axi_sel", 20),
	GATE_ICG1(CLK_INFRA_L2C_SRAM, "infra_l2c_sram", "mm_sel", 22),
	GATE_ICG1(CLK_INFRA_CCIF_AP, "infra_ccif_ap", "axi_sel", 23),
	GATE_ICG1(CLK_INFRA_AUDIO, "infra_audio", "axi_sel", 25),
	GATE_ICG1(CLK_INFRA_CCIF_MD, "infra_ccif_md", "axi_sel", 26),
	GATE_ICG1_FLAGS(CLK_INFRA_DRAMC_F26M, "infra_dramc_f26m",
			"clk26m", 31, CLK_IS_CRITICAL),
	GATE_ICG2(CLK_INFRA_I2C4, "infra_i2c4", "axi_sel", 0),
	GATE_ICG2(CLK_INFRA_I2C_APPM, "infra_i2c_appm", "axi_sel", 1),
	GATE_ICG2(CLK_INFRA_I2C_GPUPM, "infra_i2c_gpupm", "axi_sel", 2),
	GATE_ICG2(CLK_INFRA_I2C2_IMM, "infra_i2c2_imm", "axi_sel", 3),
	GATE_ICG2(CLK_INFRA_I2C2_ARB, "infra_i2c2_arb", "axi_sel", 4),
	GATE_ICG2(CLK_INFRA_I2C3_IMM, "infra_i2c3_imm", "axi_sel", 5),
	GATE_ICG2(CLK_INFRA_I2C3_ARB, "infra_i2c3_arb", "axi_sel", 6),
	GATE_ICG2(CLK_INFRA_I2C5, "infra_i2c5", "axi_sel", 7),
	GATE_ICG2(CLK_INFRA_SYS_CIRQ, "infra_sys_cirq", "axi_sel", 8),
	GATE_ICG2(CLK_INFRA_SPI1, "infra_spi1", "spi_sel", 10),
	GATE_ICG2_FLAGS(CLK_INFRA_DRAMC_B_F26M, "infra_dramc_b_f26m",
			"clk26m", 11, CLK_IS_CRITICAL),
	GATE_ICG2(CLK_INFRA_ANC_MD32, "infra_anc_md32", "anc_md32_sel", 12),
	GATE_ICG2(CLK_INFRA_ANC_MD32_32K, "infra_anc_md32_32k", "clk26m", 13),
	GATE_ICG2(CLK_INFRA_DVFS_SPM1, "infra_dvfs_spm1", "axi_sel", 15),
	GATE_ICG2(CLK_INFRA_AES_TOP0, "infra_aes_top0", "axi_sel", 16),
	GATE_ICG2(CLK_INFRA_AES_TOP1, "infra_aes_top1", "axi_sel", 17),
	GATE_ICG2(CLK_INFRA_SSUSB_BUS, "infra_ssusb_bus", "axi_sel", 18),
	GATE_ICG2(CLK_INFRA_SPI2, "infra_spi2", "spi_sel", 19),
	GATE_ICG2(CLK_INFRA_SPI3, "infra_spi3", "spi_sel", 20),
	GATE_ICG2(CLK_INFRA_SPI4, "infra_spi4", "spi_sel", 21),
	GATE_ICG2(CLK_INFRA_SPI5, "infra_spi5", "spi_sel", 22),
	GATE_ICG2(CLK_INFRA_IRTX, "infra_irtx", "spi_sel", 23),
	GATE_ICG2(CLK_INFRA_SSUSB_SYS, "infra_ssusb_sys",
		  "ssusb_top_sys_sel", 24),
	GATE_ICG2(CLK_INFRA_SSUSB_REF, "infra_ssusb_ref", "clk26m", 9),
	GATE_ICG2(CLK_INFRA_AUDIO_26M, "infra_audio_26m", "clk26m", 26),
	GATE_ICG2(CLK_INFRA_AUDIO_26M_PAD_TOP, "infra_audio_26m_pad_top",
		  "clk26m", 27),
	GATE_ICG2(CLK_INFRA_MODEM_TEMP_SHARE, "infra_modem_temp_share",
		  "axi_sel", 28),
	GATE_ICG2(CLK_INFRA_VAD_WRAP_SOC, "infra_vad_wrap_soc", "axi_sel", 29),
	GATE_ICG2(CLK_INFRA_DRAMC_CONF, "infra_dramc_conf", "axi_sel", 30),
	GATE_ICG2(CLK_INFRA_DRAMC_B_CONF, "infra_dramc_b_conf", "axi_sel", 31),
	GATE_ICG1(CLK_INFRA_MFG_VCG, "infra_mfg_vcg", "mfg_52m_sel", 14),
};

static const struct mtk_fixed_factor infra_fixed_divs[] = {
	FACTOR(CLK_INFRA_13M, "clk13m", "clk26m", 1, 2),
};

static struct clk_hw_onecell_data *infra_clk_data;

static void mtk_infrasys_init_early(struct device_node *node)
{
	int r, i;

	if (!infra_clk_data) {
		infra_clk_data = mtk_alloc_clk_data(CLK_INFRA_NR);
		if (!infra_clk_data)
			return;

		for (i = 0; i < CLK_INFRA_NR; i++)
			infra_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);
	}

	mtk_clk_register_factors(infra_fixed_divs, ARRAY_SIZE(infra_fixed_divs),
				 infra_clk_data);

	r = of_clk_add_hw_provider(node, of_clk_hw_onecell_get,
				   infra_clk_data);
	if (r)
		pr_err("%s(): could not register clock provider: %d\n",
		       __func__, r);
}

CLK_OF_DECLARE_DRIVER(mtk_infra, "mediatek,mt6797-infracfg",
		      mtk_infrasys_init_early);

static int mtk_infrasys_init(struct platform_device *pdev)
{
	int i;
	struct device_node *node = pdev->dev.of_node;

	if (!infra_clk_data) {
		infra_clk_data = mtk_alloc_clk_data(CLK_INFRA_NR);
		if (!infra_clk_data)
			return -ENOMEM;
	} else {
		for (i = 0; i < CLK_INFRA_NR; i++) {
			if (infra_clk_data->hws[i] == ERR_PTR(-EPROBE_DEFER))
				infra_clk_data->hws[i] = ERR_PTR(-ENOENT);
		}
	}

	mtk_clk_register_gates(&pdev->dev, node, infra_clks,
			       ARRAY_SIZE(infra_clks), infra_clk_data);
	mtk_clk_register_factors(infra_fixed_divs, ARRAY_SIZE(infra_fixed_divs),
				 infra_clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get,
				      infra_clk_data);
}

#define MT6797_PLL_FMAX		(3000UL * MHZ)

#define CON0_MT6797_RST_BAR	BIT(24)

#define PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift, _div_table) {			\
	.id = _id,						\
	.name = _name,						\
	.reg = _reg,						\
	.pwr_reg = _pwr_reg,					\
	.en_mask = _en_mask,					\
	.flags = _flags,					\
	.rst_bar_mask = CON0_MT6797_RST_BAR,			\
	.fmax = MT6797_PLL_FMAX,				\
	.pcwbits = _pcwbits,					\
	.pd_reg = _pd_reg,					\
	.pd_shift = _pd_shift,					\
	.tuner_reg = _tuner_reg,				\
	.pcw_reg = _pcw_reg,					\
	.pcw_shift = _pcw_shift,				\
	.div_table = _div_table,				\
}

#define PLL(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits,	\
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg,	\
			_pcw_shift)					\
		PLL_B(_id, _name, _reg, _pwr_reg, _en_mask, _flags, _pcwbits, \
			_pd_reg, _pd_shift, _tuner_reg, _pcw_reg, _pcw_shift, \
			NULL)

static const struct mtk_pll_data plls[] = {
	PLL(CLK_APMIXED_MAINPLL, "mainpll", 0x0220, 0x022C, 0xF0000100, PLL_AO,
	    21, 0x220, 4, 0x0, 0x224, 0),
	PLL(CLK_APMIXED_UNIVPLL, "univpll", 0x0230, 0x023C, 0xFE000010, 0, 7,
	    0x230, 4, 0x0, 0x234, 14),
	PLL(CLK_APMIXED_MFGPLL, "mfgpll", 0x0240, 0x024C, 0x00000100, 0, 21,
	    0x244, 24, 0x0, 0x244, 0),
	PLL(CLK_APMIXED_MSDCPLL, "msdcpll", 0x0250, 0x025C, 0x00000120, 0, 21,
	    0x250, 4, 0x0, 0x254, 0),
	PLL(CLK_APMIXED_IMGPLL, "imgpll", 0x0260, 0x026C, 0x00000120, 0, 21,
	    0x260, 4, 0x0, 0x264, 0),
	PLL(CLK_APMIXED_TVDPLL, "tvdpll", 0x0270, 0x027C, 0xC0000120, 0, 21,
	    0x270, 4, 0x0, 0x274, 0),
	PLL(CLK_APMIXED_CODECPLL, "codecpll", 0x0290, 0x029C, 0x00000120, 0, 21,
	    0x290, 4, 0x0, 0x294, 0),
	PLL(CLK_APMIXED_VDECPLL, "vdecpll", 0x02E4, 0x02F0, 0x00000120, 0, 21,
	    0x2E4, 4, 0x0, 0x2E8, 0),
	PLL(CLK_APMIXED_APLL1, "apll1", 0x02A0, 0x02B0, 0x00000130, 0, 31,
	    0x2A0, 4, 0x2A8, 0x2A4, 0),
	PLL(CLK_APMIXED_APLL2, "apll2", 0x02B4, 0x02C4, 0x00000130, 0, 31,
	    0x2B4, 4, 0x2BC, 0x2B8, 0),
};

static int mtk_apmixedsys_init(struct platform_device *pdev)
{
	struct clk_hw_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_APMIXED_NR);
	if (!clk_data)
		return -ENOMEM;

	mtk_clk_register_plls(node, plls, ARRAY_SIZE(plls), clk_data);

	return of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
}

static const struct of_device_id of_match_clk_mt6797[] = {
	{
		.compatible = "mediatek,mt6797-topckgen",
		.data = mtk_topckgen_init,
	}, {
		.compatible = "mediatek,mt6797-infracfg",
		.data = mtk_infrasys_init,
	}, {
		.compatible = "mediatek,mt6797-apmixedsys",
		.data = mtk_apmixedsys_init,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt6797);

static int clk_mt6797_probe(struct platform_device *pdev)
{
	int (*clk_init)(struct platform_device *);
	int r;

	clk_init = of_device_get_match_data(&pdev->dev);
	if (!clk_init)
		return -EINVAL;

	r = clk_init(pdev);
	if (r)
		dev_err(&pdev->dev,
			"could not register clock provider: %s: %d\n",
			pdev->name, r);

	return r;
}

static struct platform_driver clk_mt6797_drv = {
	.probe = clk_mt6797_probe,
	.driver = {
		.name = "clk-mt6797",
		.of_match_table = of_match_clk_mt6797,
	},
};

static int __init clk_mt6797_init(void)
{
	return platform_driver_register(&clk_mt6797_drv);
}

arch_initcall(clk_mt6797_init);
MODULE_LICENSE("GPL");
