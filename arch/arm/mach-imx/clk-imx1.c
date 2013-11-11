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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/err.h>

#include "clk.h"
#include "common.h"
#include "hardware.h"

/* CCM register addresses */
#define IO_ADDR_CCM(off)	(MX1_IO_ADDRESS(MX1_CCM_BASE_ADDR + (off)))

#define CCM_CSCR	IO_ADDR_CCM(0x0)
#define CCM_MPCTL0	IO_ADDR_CCM(0x4)
#define CCM_SPCTL0	IO_ADDR_CCM(0xc)
#define CCM_PCDR	IO_ADDR_CCM(0x20)

/* SCM register addresses */
#define IO_ADDR_SCM(off)	(MX1_IO_ADDRESS(MX1_SCM_BASE_ADDR + (off)))

#define SCM_GCCR	IO_ADDR_SCM(0xc)

static const char *prem_sel_clks[] = { "clk32_premult", "clk16m", };
static const char *clko_sel_clks[] = { "per1", "hclk", "clk48m", "clk16m", "prem",
				"fclk", };
enum imx1_clks {
	dummy, clk32, clk16m_ext, clk16m, clk32_premult, prem, mpll, spll, mcu,
	fclk, hclk, clk48m, per1, per2, per3, clko, dma_gate, csi_gate,
	mma_gate, usbd_gate, clk_max
};

static struct clk *clk[clk_max];

int __init mx1_clocks_init(unsigned long fref)
{
	int i;

	clk[dummy] = imx_clk_fixed("dummy", 0);
	clk[clk32] = imx_clk_fixed("clk32", fref);
	clk[clk16m_ext] = imx_clk_fixed("clk16m_ext", 16000000);
	clk[clk16m] = imx_clk_gate("clk16m", "clk16m_ext", CCM_CSCR, 17);
	clk[clk32_premult] = imx_clk_fixed_factor("clk32_premult", "clk32", 512, 1);
	clk[prem] = imx_clk_mux("prem", CCM_CSCR, 16, 1, prem_sel_clks,
			ARRAY_SIZE(prem_sel_clks));
	clk[mpll] = imx_clk_pllv1("mpll", "clk32_premult", CCM_MPCTL0);
	clk[spll] = imx_clk_pllv1("spll", "prem", CCM_SPCTL0);
	clk[mcu] = imx_clk_divider("mcu", "clk32_premult", CCM_CSCR, 15, 1);
	clk[fclk] = imx_clk_divider("fclk", "mpll", CCM_CSCR, 15, 1);
	clk[hclk] = imx_clk_divider("hclk", "spll", CCM_CSCR, 10, 4);
	clk[clk48m] = imx_clk_divider("clk48m", "spll", CCM_CSCR, 26, 3);
	clk[per1] = imx_clk_divider("per1", "spll", CCM_PCDR, 0, 4);
	clk[per2] = imx_clk_divider("per2", "spll", CCM_PCDR, 4, 4);
	clk[per3] = imx_clk_divider("per3", "spll", CCM_PCDR, 16, 7);
	clk[clko] = imx_clk_mux("clko", CCM_CSCR, 29, 3, clko_sel_clks,
			ARRAY_SIZE(clko_sel_clks));
	clk[dma_gate] = imx_clk_gate("dma_gate", "hclk", SCM_GCCR, 4);
	clk[csi_gate] = imx_clk_gate("csi_gate", "hclk", SCM_GCCR, 2);
	clk[mma_gate] = imx_clk_gate("mma_gate", "hclk", SCM_GCCR, 1);
	clk[usbd_gate] = imx_clk_gate("usbd_gate", "clk48m", SCM_GCCR, 0);

	for (i = 0; i < ARRAY_SIZE(clk); i++)
		if (IS_ERR(clk[i]))
			pr_err("imx1 clk %d: register failed with %ld\n",
				i, PTR_ERR(clk[i]));

	clk_register_clkdev(clk[dma_gate], "ahb", "imx1-dma");
	clk_register_clkdev(clk[hclk], "ipg", "imx1-dma");
	clk_register_clkdev(clk[csi_gate], NULL, "mx1-camera.0");
	clk_register_clkdev(clk[mma_gate], "mma", NULL);
	clk_register_clkdev(clk[usbd_gate], NULL, "imx_udc.0");
	clk_register_clkdev(clk[per1], "per", "imx-gpt.0");
	clk_register_clkdev(clk[hclk], "ipg", "imx-gpt.0");
	clk_register_clkdev(clk[per1], "per", "imx1-uart.0");
	clk_register_clkdev(clk[hclk], "ipg", "imx1-uart.0");
	clk_register_clkdev(clk[per1], "per", "imx1-uart.1");
	clk_register_clkdev(clk[hclk], "ipg", "imx1-uart.1");
	clk_register_clkdev(clk[per1], "per", "imx1-uart.2");
	clk_register_clkdev(clk[hclk], "ipg", "imx1-uart.2");
	clk_register_clkdev(clk[hclk], NULL, "imx1-i2c.0");
	clk_register_clkdev(clk[per2], "per", "imx1-cspi.0");
	clk_register_clkdev(clk[dummy], "ipg", "imx1-cspi.0");
	clk_register_clkdev(clk[per2], "per", "imx1-cspi.1");
	clk_register_clkdev(clk[dummy], "ipg", "imx1-cspi.1");
	clk_register_clkdev(clk[per2], NULL, "imx-mmc.0");
	clk_register_clkdev(clk[per2], "per", "imx1-fb.0");
	clk_register_clkdev(clk[dummy], "ipg", "imx1-fb.0");
	clk_register_clkdev(clk[dummy], "ahb", "imx1-fb.0");
	clk_register_clkdev(clk[hclk], "mshc", NULL);
	clk_register_clkdev(clk[per3], "ssi", NULL);
	clk_register_clkdev(clk[clk32], NULL, "imx1-rtc.0");
	clk_register_clkdev(clk[clko], "clko", NULL);

	mxc_timer_init(MX1_IO_ADDRESS(MX1_TIM1_BASE_ADDR), MX1_TIM1_INT);

	return 0;
}
