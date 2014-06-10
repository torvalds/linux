/*
 *  Copyright (C) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <dt-bindings/clock/imx1-clock.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"

static const char *prem_sel_clks[] = { "clk32_premult", "clk16m", };
static const char *clko_sel_clks[] = { "per1", "hclk", "clk48m", "clk16m",
				       "prem", "fclk", };

static struct clk *clk[IMX1_CLK_MAX];
static struct clk_onecell_data clk_data;

static void __iomem *ccm __initdata;
#define CCM_CSCR	(ccm + 0x0000)
#define CCM_MPCTL0	(ccm + 0x0004)
#define CCM_SPCTL0	(ccm + 0x000c)
#define CCM_PCDR	(ccm + 0x0020)
#define SCM_GCCR	(ccm + 0x0810)

static void __init _mx1_clocks_init(unsigned long fref)
{
	clk[IMX1_CLK_DUMMY] = imx_clk_fixed("dummy", 0);
	clk[IMX1_CLK_CLK32] = imx_obtain_fixed_clock("clk32", fref);
	clk[IMX1_CLK_CLK16M_EXT] = imx_clk_fixed("clk16m_ext", 16000000);
	clk[IMX1_CLK_CLK16M] = imx_clk_gate("clk16m", "clk16m_ext", CCM_CSCR, 17);
	clk[IMX1_CLK_CLK32_PREMULT] = imx_clk_fixed_factor("clk32_premult", "clk32", 512, 1);
	clk[IMX1_CLK_PREM] = imx_clk_mux("prem", CCM_CSCR, 16, 1, prem_sel_clks, ARRAY_SIZE(prem_sel_clks));
	clk[IMX1_CLK_MPLL] = imx_clk_pllv1("mpll", "clk32_premult", CCM_MPCTL0);
	clk[IMX1_CLK_MPLL_GATE] = imx_clk_gate("mpll_gate", "mpll", CCM_CSCR, 0);
	clk[IMX1_CLK_SPLL] = imx_clk_pllv1("spll", "prem", CCM_SPCTL0);
	clk[IMX1_CLK_SPLL_GATE] = imx_clk_gate("spll_gate", "spll", CCM_CSCR, 1);
	clk[IMX1_CLK_MCU] = imx_clk_divider("mcu", "clk32_premult", CCM_CSCR, 15, 1);
	clk[IMX1_CLK_FCLK] = imx_clk_divider("fclk", "mpll_gate", CCM_CSCR, 15, 1);
	clk[IMX1_CLK_HCLK] = imx_clk_divider("hclk", "spll_gate", CCM_CSCR, 10, 4);
	clk[IMX1_CLK_CLK48M] = imx_clk_divider("clk48m", "spll_gate", CCM_CSCR, 26, 3);
	clk[IMX1_CLK_PER1] = imx_clk_divider("per1", "spll_gate", CCM_PCDR, 0, 4);
	clk[IMX1_CLK_PER2] = imx_clk_divider("per2", "spll_gate", CCM_PCDR, 4, 4);
	clk[IMX1_CLK_PER3] = imx_clk_divider("per3", "spll_gate", CCM_PCDR, 16, 7);
	clk[IMX1_CLK_CLKO] = imx_clk_mux("clko", CCM_CSCR, 29, 3, clko_sel_clks, ARRAY_SIZE(clko_sel_clks));
	clk[IMX1_CLK_UART3_GATE] = imx_clk_gate("uart3_gate", "hclk", SCM_GCCR, 6);
	clk[IMX1_CLK_SSI2_GATE] = imx_clk_gate("ssi2_gate", "hclk", SCM_GCCR, 5);
	clk[IMX1_CLK_BROM_GATE] = imx_clk_gate("brom_gate", "hclk", SCM_GCCR, 4);
	clk[IMX1_CLK_DMA_GATE] = imx_clk_gate("dma_gate", "hclk", SCM_GCCR, 3);
	clk[IMX1_CLK_CSI_GATE] = imx_clk_gate("csi_gate", "hclk", SCM_GCCR, 2);
	clk[IMX1_CLK_MMA_GATE] = imx_clk_gate("mma_gate", "hclk", SCM_GCCR, 1);
	clk[IMX1_CLK_USBD_GATE] = imx_clk_gate("usbd_gate", "clk48m", SCM_GCCR, 0);

	imx_check_clocks(clk, ARRAY_SIZE(clk));
}

int __init mx1_clocks_init(unsigned long fref)
{
	ccm = MX1_IO_ADDRESS(MX1_CCM_BASE_ADDR);

	_mx1_clocks_init(fref);

	clk_register_clkdev(clk[IMX1_CLK_PER1], "per", "imx-gpt.0");
	clk_register_clkdev(clk[IMX1_CLK_HCLK], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[IMX1_CLK_DMA_GATE], "ahb", "imx1-dma");
	clk_register_clkdev(clk[IMX1_CLK_HCLK], "ipg", "imx1-dma");
	clk_register_clkdev(clk[IMX1_CLK_PER1], "per", "imx1-uart.0");
	clk_register_clkdev(clk[IMX1_CLK_HCLK], "ipg", "imx1-uart.0");
	clk_register_clkdev(clk[IMX1_CLK_PER1], "per", "imx1-uart.1");
	clk_register_clkdev(clk[IMX1_CLK_HCLK], "ipg", "imx1-uart.1");
	clk_register_clkdev(clk[IMX1_CLK_PER1], "per", "imx1-uart.2");
	clk_register_clkdev(clk[IMX1_CLK_UART3_GATE], "ipg", "imx1-uart.2");
	clk_register_clkdev(clk[IMX1_CLK_HCLK], NULL, "imx1-i2c.0");
	clk_register_clkdev(clk[IMX1_CLK_PER2], "per", "imx1-cspi.0");
	clk_register_clkdev(clk[IMX1_CLK_DUMMY], "ipg", "imx1-cspi.0");
	clk_register_clkdev(clk[IMX1_CLK_PER2], "per", "imx1-cspi.1");
	clk_register_clkdev(clk[IMX1_CLK_DUMMY], "ipg", "imx1-cspi.1");
	clk_register_clkdev(clk[IMX1_CLK_PER2], "per", "imx1-fb.0");
	clk_register_clkdev(clk[IMX1_CLK_DUMMY], "ipg", "imx1-fb.0");
	clk_register_clkdev(clk[IMX1_CLK_DUMMY], "ahb", "imx1-fb.0");

	mxc_timer_init(MX1_IO_ADDRESS(MX1_TIM1_BASE_ADDR), MX1_TIM1_INT);

	return 0;
}

static void __init mx1_clocks_init_dt(struct device_node *np)
{
	ccm = of_iomap(np, 0);
	BUG_ON(!ccm);

	_mx1_clocks_init(32768);

	clk_data.clks = clk;
	clk_data.clk_num = ARRAY_SIZE(clk);
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);

	mxc_timer_init_dt(of_find_compatible_node(NULL, NULL, "fsl,imx1-gpt"));
}
CLK_OF_DECLARE(imx1_ccm, "fsl,imx1-ccm", mx1_clocks_init_dt);
