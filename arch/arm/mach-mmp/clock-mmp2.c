#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <mach/addr-map.h>

#include "common.h"
#include "clock.h"

/*
 * APB Clock register offsets for MMP2
 */
#define APBC_RTC	APBC_REG(0x000)
#define APBC_TWSI1	APBC_REG(0x004)
#define APBC_TWSI2	APBC_REG(0x008)
#define APBC_TWSI3	APBC_REG(0x00c)
#define APBC_TWSI4	APBC_REG(0x010)
#define APBC_KPC	APBC_REG(0x018)
#define APBC_UART1	APBC_REG(0x02c)
#define APBC_UART2	APBC_REG(0x030)
#define APBC_UART3	APBC_REG(0x034)
#define APBC_GPIO	APBC_REG(0x038)
#define APBC_PWM0	APBC_REG(0x03c)
#define APBC_PWM1	APBC_REG(0x040)
#define APBC_PWM2	APBC_REG(0x044)
#define APBC_PWM3	APBC_REG(0x048)
#define APBC_SSP0	APBC_REG(0x04c)
#define APBC_SSP1	APBC_REG(0x050)
#define APBC_SSP2	APBC_REG(0x054)
#define APBC_SSP3	APBC_REG(0x058)
#define APBC_SSP4	APBC_REG(0x05c)
#define APBC_SSP5	APBC_REG(0x060)
#define APBC_TWSI5	APBC_REG(0x07c)
#define APBC_TWSI6	APBC_REG(0x080)
#define APBC_UART4	APBC_REG(0x088)

#define APMU_USB	APMU_REG(0x05c)
#define APMU_NAND	APMU_REG(0x060)
#define APMU_SDH0	APMU_REG(0x054)
#define APMU_SDH1	APMU_REG(0x058)
#define APMU_SDH2	APMU_REG(0x0e8)
#define APMU_SDH3	APMU_REG(0x0ec)

static void sdhc_clk_enable(struct clk *clk)
{
	uint32_t clk_rst;

	clk_rst  =  __raw_readl(clk->clk_rst);
	clk_rst |= clk->enable_val;
	__raw_writel(clk_rst, clk->clk_rst);
}

static void sdhc_clk_disable(struct clk *clk)
{
	uint32_t clk_rst;

	clk_rst  =  __raw_readl(clk->clk_rst);
	clk_rst &= ~clk->enable_val;
	__raw_writel(clk_rst, clk->clk_rst);
}

struct clkops sdhc_clk_ops = {
	.enable		= sdhc_clk_enable,
	.disable	= sdhc_clk_disable,
};

/* APB peripheral clocks */
static APBC_CLK(uart1, UART1, 1, 26000000);
static APBC_CLK(uart2, UART2, 1, 26000000);
static APBC_CLK(uart3, UART3, 1, 26000000);
static APBC_CLK(uart4, UART4, 1, 26000000);
static APBC_CLK(twsi1, TWSI1, 0, 26000000);
static APBC_CLK(twsi2, TWSI2, 0, 26000000);
static APBC_CLK(twsi3, TWSI3, 0, 26000000);
static APBC_CLK(twsi4, TWSI4, 0, 26000000);
static APBC_CLK(twsi5, TWSI5, 0, 26000000);
static APBC_CLK(twsi6, TWSI6, 0, 26000000);
static APBC_CLK(gpio, GPIO, 0, 26000000);

static APMU_CLK(nand, NAND, 0xbf, 100000000);
static APMU_CLK_OPS(sdh0, SDH0, 0x1b, 200000000, &sdhc_clk_ops);
static APMU_CLK_OPS(sdh1, SDH1, 0x1b, 200000000, &sdhc_clk_ops);
static APMU_CLK_OPS(sdh2, SDH2, 0x1b, 200000000, &sdhc_clk_ops);
static APMU_CLK_OPS(sdh3, SDH3, 0x1b, 200000000, &sdhc_clk_ops);

static struct clk_lookup mmp2_clkregs[] = {
	INIT_CLKREG(&clk_uart1, "pxa2xx-uart.0", NULL),
	INIT_CLKREG(&clk_uart2, "pxa2xx-uart.1", NULL),
	INIT_CLKREG(&clk_uart3, "pxa2xx-uart.2", NULL),
	INIT_CLKREG(&clk_uart4, "pxa2xx-uart.3", NULL),
	INIT_CLKREG(&clk_twsi1, "pxa2xx-i2c.0", NULL),
	INIT_CLKREG(&clk_twsi2, "pxa2xx-i2c.1", NULL),
	INIT_CLKREG(&clk_twsi3, "pxa2xx-i2c.2", NULL),
	INIT_CLKREG(&clk_twsi4, "pxa2xx-i2c.3", NULL),
	INIT_CLKREG(&clk_twsi5, "pxa2xx-i2c.4", NULL),
	INIT_CLKREG(&clk_twsi6, "pxa2xx-i2c.5", NULL),
	INIT_CLKREG(&clk_nand, "pxa3xx-nand", NULL),
	INIT_CLKREG(&clk_gpio, "pxa-gpio", NULL),
	INIT_CLKREG(&clk_sdh0, "sdhci-pxav3.0", "PXA-SDHCLK"),
	INIT_CLKREG(&clk_sdh1, "sdhci-pxav3.1", "PXA-SDHCLK"),
	INIT_CLKREG(&clk_sdh2, "sdhci-pxav3.2", "PXA-SDHCLK"),
	INIT_CLKREG(&clk_sdh3, "sdhci-pxav3.3", "PXA-SDHCLK"),
};

void __init mmp2_clk_init(void)
{
	clkdev_add_table(ARRAY_AND_SIZE(mmp2_clkregs));
}
