/*
 *  Copyright (C) 2008 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
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

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/math64.h>
#include <linux/err.h>

#include <asm/io.h>
#include <mach/imx-regs.h>

/*
 * Very simple approach: We can't disable clocks, so we do
 * not need refcounting
 */

struct clk {
	struct list_head node;
	const char *name;
	unsigned long (*get_rate)(void);
};

/*
 *  get the system pll clock in Hz
 *
 *                  mfi + mfn / (mfd +1)
 *  f = 2 * f_ref * --------------------
 *                        pd + 1
 */
static unsigned long imx_decode_pll(unsigned int pll, u32 f_ref)
{
	unsigned long long ll;
	unsigned long quot;

	u32 mfi = (pll >> 10) & 0xf;
	u32 mfn = pll & 0x3ff;
	u32 mfd = (pll >> 16) & 0x3ff;
	u32 pd =  (pll >> 26) & 0xf;

	mfi = mfi <= 5 ? 5 : mfi;

	ll = 2 * (unsigned long long)f_ref *
		((mfi << 16) + (mfn << 16) / (mfd + 1));
	quot = (pd + 1) * (1 << 16);
	ll += quot / 2;
	do_div(ll, quot);
	return (unsigned long)ll;
}

static unsigned long imx_get_system_clk(void)
{
	u32 f_ref = (CSCR & CSCR_SYSTEM_SEL) ? 16000000 : (CLK32 * 512);

	return imx_decode_pll(SPCTL0, f_ref);
}

static unsigned long imx_get_mcu_clk(void)
{
	return imx_decode_pll(MPCTL0, CLK32 * 512);
}

/*
 *  get peripheral clock 1 ( UART[12], Timer[12], PWM )
 */
static unsigned long imx_get_perclk1(void)
{
	return imx_get_system_clk() / (((PCDR) & 0xf)+1);
}

/*
 *  get peripheral clock 2 ( LCD, SD, SPI[12] )
 */
static unsigned long imx_get_perclk2(void)
{
	return imx_get_system_clk() / (((PCDR>>4) & 0xf)+1);
}

/*
 *  get peripheral clock 3 ( SSI )
 */
static unsigned long imx_get_perclk3(void)
{
	return imx_get_system_clk() / (((PCDR>>16) & 0x7f)+1);
}

/*
 *  get hclk ( SDRAM, CSI, Memory Stick, I2C, DMA )
 */
static unsigned long imx_get_hclk(void)
{
	return imx_get_system_clk() / (((CSCR>>10) & 0xf)+1);
}

static struct clk clk_system_clk = {
	.name = "system_clk",
	.get_rate = imx_get_system_clk,
};

static struct clk clk_hclk = {
	.name = "hclk",
	.get_rate = imx_get_hclk,
};

static struct clk clk_mcu_clk = {
	.name = "mcu_clk",
	.get_rate = imx_get_mcu_clk,
};

static struct clk clk_perclk1 = {
	.name = "perclk1",
	.get_rate = imx_get_perclk1,
};

static struct clk clk_uart_clk = {
	.name = "uart_clk",
	.get_rate = imx_get_perclk1,
};

static struct clk clk_perclk2 = {
	.name = "perclk2",
	.get_rate = imx_get_perclk2,
};

static struct clk clk_perclk3 = {
	.name = "perclk3",
	.get_rate = imx_get_perclk3,
};

static struct clk *clks[] = {
	&clk_perclk1,
	&clk_perclk2,
	&clk_perclk3,
	&clk_system_clk,
	&clk_hclk,
	&clk_mcu_clk,
	&clk_uart_clk,
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		if (!strcmp(p->name, id)) {
			clk = p;
			goto found;
		}
	}

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->get_rate();
}
EXPORT_SYMBOL(clk_get_rate);

int imx_clocks_init(void)
{
	int i;

	mutex_lock(&clocks_mutex);
	for (i = 0; i < ARRAY_SIZE(clks); i++)
		list_add(&clks[i]->node, &clocks);
	mutex_unlock(&clocks_mutex);

	return 0;
}

