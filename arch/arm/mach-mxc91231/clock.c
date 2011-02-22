#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/clkdev.h>

#include <mach/clock.h>
#include <mach/hardware.h>
#include <mach/common.h>

#include <asm/bug.h>
#include <asm/div64.h>

#include "crm_regs.h"

#define CRM_SMALL_DIVIDER(base, name) \
	crm_small_divider(base, \
			  base ## _ ## name ## _OFFSET, \
			  base ## _ ## name ## _MASK)
#define CRM_1DIVIDER(base, name) \
	crm_divider(base, \
		    base ## _ ## name ## _OFFSET, \
		    base ## _ ## name ## _MASK, 1)
#define CRM_16DIVIDER(base, name) \
	crm_divider(base, \
		    base ## _ ## name ## _OFFSET, \
		    base ## _ ## name ## _MASK, 16)

static u32 crm_small_divider(void __iomem *reg, u8 offset, u32 mask)
{
	static const u32 crm_small_dividers[] = {
		2, 3, 4, 5, 6, 8, 10, 12
	};
	u8 idx;

	idx = (__raw_readl(reg) & mask) >> offset;
	if (idx > 7)
		return 1;

	return crm_small_dividers[idx];
}

static u32 crm_divider(void __iomem *reg, u8 offset, u32 mask, u32 z)
{
	u32 div;
	div = (__raw_readl(reg) & mask) >> offset;
	return div ? div : z;
}

static int _clk_1bit_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 1 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_1bit_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(1 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static int _clk_3bit_enable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg |= 0x7 << clk->enable_shift;
	__raw_writel(reg, clk->enable_reg);

	return 0;
}

static void _clk_3bit_disable(struct clk *clk)
{
	u32 reg;

	reg = __raw_readl(clk->enable_reg);
	reg &= ~(0x7 << clk->enable_shift);
	__raw_writel(reg, clk->enable_reg);
}

static unsigned long ckih_rate;

static unsigned long clk_ckih_get_rate(struct clk *clk)
{
	return ckih_rate;
}

static struct clk ckih_clk = {
	.get_rate = clk_ckih_get_rate,
};

static unsigned long clk_ckih_x2_get_rate(struct clk *clk)
{
	return 2 * clk_get_rate(clk->parent);
}

static struct clk ckih_x2_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_ckih_x2_get_rate,
};

static unsigned long clk_ckil_get_rate(struct clk *clk)
{
	return CKIL_CLK_FREQ;
}

static struct clk ckil_clk = {
	.get_rate = clk_ckil_get_rate,
};

/* plls stuff */
static struct clk mcu_pll_clk;
static struct clk dsp_pll_clk;
static struct clk usb_pll_clk;

static struct clk *pll_clk(u8 sel)
{
	switch (sel) {
	case 0:
		return &mcu_pll_clk;
	case 1:
		return &dsp_pll_clk;
	case 2:
		return &usb_pll_clk;
	}
	BUG();
}

static void __iomem *pll_base(struct clk *clk)
{
	if (clk == &mcu_pll_clk)
		return MXC_PLL0_BASE;
	else if (clk == &dsp_pll_clk)
		return MXC_PLL1_BASE;
	else if (clk == &usb_pll_clk)
		return MXC_PLL2_BASE;
	BUG();
}

static unsigned long clk_pll_get_rate(struct clk *clk)
{
	const void __iomem *pllbase;
	unsigned long dp_op, dp_mfd, dp_mfn, pll_hfsm, ref_clk, mfi;
	long mfn, mfn_abs, mfd, pdf;
	s64 temp;
	pllbase = pll_base(clk);

	pll_hfsm = __raw_readl(pllbase + MXC_PLL_DP_CTL) & MXC_PLL_DP_CTL_HFSM;
	if (pll_hfsm == 0) {
		dp_op = __raw_readl(pllbase + MXC_PLL_DP_OP);
		dp_mfd = __raw_readl(pllbase + MXC_PLL_DP_MFD);
		dp_mfn = __raw_readl(pllbase + MXC_PLL_DP_MFN);
	} else {
		dp_op = __raw_readl(pllbase + MXC_PLL_DP_HFS_OP);
		dp_mfd = __raw_readl(pllbase + MXC_PLL_DP_HFS_MFD);
		dp_mfn = __raw_readl(pllbase + MXC_PLL_DP_HFS_MFN);
	}

	pdf = dp_op & MXC_PLL_DP_OP_PDF_MASK;
	mfi = (dp_op >> MXC_PLL_DP_OP_MFI_OFFSET) & MXC_PLL_DP_OP_PDF_MASK;
	mfi = (mfi <= 5) ? 5 : mfi;
	mfd = dp_mfd & MXC_PLL_DP_MFD_MASK;
	mfn = dp_mfn & MXC_PLL_DP_MFN_MASK;
	mfn = (mfn <= 0x4000000) ? mfn : (mfn - 0x10000000);

	if (mfn < 0)
		mfn_abs = -mfn;
	else
		mfn_abs = mfn;

/* XXX: actually this asumes that ckih is fed to pll, but spec says
 * that ckih_x2 is also possible. need to check this out.
 */
	ref_clk = clk_get_rate(&ckih_clk);

	ref_clk *= 2;
	ref_clk /= pdf + 1;

	temp = (u64) ref_clk * mfn_abs;
	do_div(temp, mfd);
	if (mfn < 0)
		temp = -temp;
	temp += ref_clk * mfi;

	return temp;
}

static int clk_pll_enable(struct clk *clk)
{
	void __iomem *ctl;
	u32 reg;

	ctl = pll_base(clk);
	reg = __raw_readl(ctl);
	reg |= (MXC_PLL_DP_CTL_RST | MXC_PLL_DP_CTL_UPEN);
	__raw_writel(reg, ctl);
	do {
		reg = __raw_readl(ctl);
	} while ((reg & MXC_PLL_DP_CTL_LRF) != MXC_PLL_DP_CTL_LRF);
	return 0;
}

static void clk_pll_disable(struct clk *clk)
{
	void __iomem *ctl;
	u32 reg;

	ctl = pll_base(clk);
	reg = __raw_readl(ctl);
	reg &= ~(MXC_PLL_DP_CTL_RST | MXC_PLL_DP_CTL_UPEN);
	__raw_writel(reg, ctl);
}

static struct clk mcu_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_pll_get_rate,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
};

static struct clk dsp_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_pll_get_rate,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
};

static struct clk usb_pll_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_pll_get_rate,
	.enable = clk_pll_enable,
	.disable = clk_pll_disable,
};
/* plls stuff end */

/* ap_ref_clk stuff */
static struct clk ap_ref_clk;

static unsigned long clk_ap_ref_get_rate(struct clk *clk)
{
	u32 ascsr, acsr;
	u8 ap_pat_ref_div_2, ap_isel, acs, ads;

	ascsr = __raw_readl(MXC_CRMAP_ASCSR);
	acsr = __raw_readl(MXC_CRMAP_ACSR);

	/* 0 for ckih, 1 for ckih*2 */
	ap_isel = ascsr & MXC_CRMAP_ASCSR_APISEL;
	/* reg divider */
	ap_pat_ref_div_2 = (ascsr >> MXC_CRMAP_ASCSR_AP_PATDIV2_OFFSET) & 0x1;
	/* undocumented, 1 for disabling divider */
	ads = (acsr >> MXC_CRMAP_ACSR_ADS_OFFSET) & 0x1;
	/* 0 for pat_ref, 1 for divider out */
	acs = acsr & MXC_CRMAP_ACSR_ACS;

	if (acs & !ads)
		/* use divided clock */
		return clk_get_rate(clk->parent) / (ap_pat_ref_div_2 ? 2 : 1);

	return clk_get_rate(clk->parent) * (ap_isel ? 2 : 1);
}

static struct clk ap_ref_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_ap_ref_get_rate,
};
/* ap_ref_clk stuff end */

/* ap_pre_dfs_clk stuff */
static struct clk ap_pre_dfs_clk;

static unsigned long clk_ap_pre_dfs_get_rate(struct clk *clk)
{
	u32 acsr, ascsr;

	acsr = __raw_readl(MXC_CRMAP_ACSR);
	ascsr = __raw_readl(MXC_CRMAP_ASCSR);

	if (acsr & MXC_CRMAP_ACSR_ACS) {
		u8 sel;
		sel = (ascsr & MXC_CRMAP_ASCSR_APSEL_MASK) >>
			MXC_CRMAP_ASCSR_APSEL_OFFSET;
		return clk_get_rate(pll_clk(sel)) /
			CRM_SMALL_DIVIDER(MXC_CRMAP_ACDR, ARMDIV);
	}
	return clk_get_rate(&ap_ref_clk);
}

static struct clk ap_pre_dfs_clk = {
	.get_rate = clk_ap_pre_dfs_get_rate,
};
/* ap_pre_dfs_clk stuff end */

/* usb_clk stuff */
static struct clk usb_clk;

static struct clk *clk_usb_parent(struct clk *clk)
{
	u32 acsr, ascsr;

	acsr = __raw_readl(MXC_CRMAP_ACSR);
	ascsr = __raw_readl(MXC_CRMAP_ASCSR);

	if (acsr & MXC_CRMAP_ACSR_ACS) {
		u8 sel;
		sel = (ascsr & MXC_CRMAP_ASCSR_USBSEL_MASK) >>
			MXC_CRMAP_ASCSR_USBSEL_OFFSET;
		return pll_clk(sel);
	}
	return &ap_ref_clk;
}

static unsigned long clk_usb_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) /
		CRM_SMALL_DIVIDER(MXC_CRMAP_ACDER2, USBDIV);
}

static struct clk usb_clk = {
	.enable_reg = MXC_CRMAP_ACDER2,
	.enable_shift = MXC_CRMAP_ACDER2_USBEN_OFFSET,
	.get_rate = clk_usb_get_rate,
	.enable = _clk_1bit_enable,
	.disable = _clk_1bit_disable,
};
/* usb_clk stuff end */

static unsigned long clk_ipg_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) / CRM_16DIVIDER(MXC_CRMAP_ACDR, IPDIV);
}

static unsigned long clk_ahb_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) /
		CRM_16DIVIDER(MXC_CRMAP_ACDR, AHBDIV);
}

static struct clk ipg_clk = {
	.parent = &ap_pre_dfs_clk,
	.get_rate = clk_ipg_get_rate,
};

static struct clk ahb_clk = {
	.parent = &ap_pre_dfs_clk,
	.get_rate = clk_ahb_get_rate,
};

/* perclk_clk stuff */
static struct clk perclk_clk;

static unsigned long clk_perclk_get_rate(struct clk *clk)
{
	u32 acder2;

	acder2 = __raw_readl(MXC_CRMAP_ACDER2);
	if (acder2 & MXC_CRMAP_ACDER2_BAUD_ISEL_MASK)
		return 2 * clk_get_rate(clk->parent);

	return clk_get_rate(clk->parent);
}

static struct clk perclk_clk = {
	.parent = &ckih_clk,
	.get_rate = clk_perclk_get_rate,
};
/* perclk_clk stuff end */

/* uart_clk stuff */
static struct clk uart_clk[];

static unsigned long clk_uart_get_rate(struct clk *clk)
{
	u32 div;

	switch (clk->id) {
	case 0:
	case 1:
		div = CRM_SMALL_DIVIDER(MXC_CRMAP_ACDER2, BAUDDIV);
		break;
	case 2:
		div = CRM_SMALL_DIVIDER(MXC_CRMAP_APRA, UART3DIV);
		break;
	default:
		BUG();
	}
	return clk_get_rate(clk->parent) / div;
}

static struct clk uart_clk[] = {
	{
		.id = 0,
		.parent = &perclk_clk,
		.enable_reg = MXC_CRMAP_APRA,
		.enable_shift = MXC_CRMAP_APRA_UART1EN_OFFSET,
		.get_rate = clk_uart_get_rate,
		.enable = _clk_1bit_enable,
		.disable = _clk_1bit_disable,
	}, {
		.id = 1,
		.parent = &perclk_clk,
		.enable_reg = MXC_CRMAP_APRA,
		.enable_shift = MXC_CRMAP_APRA_UART2EN_OFFSET,
		.get_rate = clk_uart_get_rate,
		.enable = _clk_1bit_enable,
		.disable = _clk_1bit_disable,
	}, {
		.id = 2,
		.parent = &perclk_clk,
		.enable_reg = MXC_CRMAP_APRA,
		.enable_shift = MXC_CRMAP_APRA_UART3EN_OFFSET,
		.get_rate = clk_uart_get_rate,
		.enable = _clk_1bit_enable,
		.disable = _clk_1bit_disable,
	},
};
/* uart_clk stuff end */

/* sdhc_clk stuff */
static struct clk nfc_clk;

static unsigned long clk_nfc_get_rate(struct clk *clk)
{
	return clk_get_rate(clk->parent) /
		CRM_1DIVIDER(MXC_CRMAP_ACDER2, NFCDIV);
}

static struct clk nfc_clk = {
	.parent = &ahb_clk,
	.enable_reg = MXC_CRMAP_ACDER2,
	.enable_shift = MXC_CRMAP_ACDER2_NFCEN_OFFSET,
	.get_rate = clk_nfc_get_rate,
	.enable = _clk_1bit_enable,
	.disable = _clk_1bit_disable,
};
/* sdhc_clk stuff end */

/* sdhc_clk stuff */
static struct clk sdhc_clk[];

static struct clk *clk_sdhc_parent(struct clk *clk)
{
	u32 aprb;
	u8 sel;
	u32 mask;
	int offset;

	aprb = __raw_readl(MXC_CRMAP_APRB);

	switch (clk->id) {
	case 0:
		mask = MXC_CRMAP_APRB_SDHC1_ISEL_MASK;
		offset = MXC_CRMAP_APRB_SDHC1_ISEL_OFFSET;
		break;
	case 1:
		mask = MXC_CRMAP_APRB_SDHC2_ISEL_MASK;
		offset = MXC_CRMAP_APRB_SDHC2_ISEL_OFFSET;
		break;
	default:
		BUG();
	}
	sel = (aprb & mask) >> offset;

	switch (sel) {
	case 0:
		return &ckih_clk;
	case 1:
		return &ckih_x2_clk;
	}
	return &usb_clk;
}

static unsigned long clk_sdhc_get_rate(struct clk *clk)
{
	u32 div;

	switch (clk->id) {
	case 0:
		div = CRM_SMALL_DIVIDER(MXC_CRMAP_APRB, SDHC1_DIV);
		break;
	case 1:
		div = CRM_SMALL_DIVIDER(MXC_CRMAP_APRB, SDHC2_DIV);
		break;
	default:
		BUG();
	}

	return clk_get_rate(clk->parent) / div;
}

static int clk_sdhc_enable(struct clk *clk)
{
	u32 amlpmre1, aprb;

	amlpmre1 = __raw_readl(MXC_CRMAP_AMLPMRE1);
	aprb = __raw_readl(MXC_CRMAP_APRB);
	switch (clk->id) {
	case 0:
		amlpmre1 |= (0x7 << MXC_CRMAP_AMLPMRE1_MLPME4_OFFSET);
		aprb |= (0x1 << MXC_CRMAP_APRB_SDHC1EN_OFFSET);
		break;
	case 1:
		amlpmre1 |= (0x7 << MXC_CRMAP_AMLPMRE1_MLPME5_OFFSET);
		aprb |= (0x1 << MXC_CRMAP_APRB_SDHC2EN_OFFSET);
		break;
	}
	__raw_writel(amlpmre1, MXC_CRMAP_AMLPMRE1);
	__raw_writel(aprb, MXC_CRMAP_APRB);
	return 0;
}

static void clk_sdhc_disable(struct clk *clk)
{
	u32 amlpmre1, aprb;

	amlpmre1 = __raw_readl(MXC_CRMAP_AMLPMRE1);
	aprb = __raw_readl(MXC_CRMAP_APRB);
	switch (clk->id) {
	case 0:
		amlpmre1 &= ~(0x7 << MXC_CRMAP_AMLPMRE1_MLPME4_OFFSET);
		aprb &= ~(0x1 << MXC_CRMAP_APRB_SDHC1EN_OFFSET);
		break;
	case 1:
		amlpmre1 &= ~(0x7 << MXC_CRMAP_AMLPMRE1_MLPME5_OFFSET);
		aprb &= ~(0x1 << MXC_CRMAP_APRB_SDHC2EN_OFFSET);
		break;
	}
	__raw_writel(amlpmre1, MXC_CRMAP_AMLPMRE1);
	__raw_writel(aprb, MXC_CRMAP_APRB);
}

static struct clk sdhc_clk[] = {
	{
		.id = 0,
		.get_rate = clk_sdhc_get_rate,
		.enable = clk_sdhc_enable,
		.disable = clk_sdhc_disable,
	}, {
		.id = 1,
		.get_rate = clk_sdhc_get_rate,
		.enable = clk_sdhc_enable,
		.disable = clk_sdhc_disable,
	},
};
/* sdhc_clk stuff end */

/* wdog_clk stuff */
static struct clk wdog_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable_reg = MXC_CRMAP_AMLPMRD,
		.enable_shift = MXC_CRMAP_AMLPMRD_MLPMD7_OFFSET,
		.enable = _clk_3bit_enable,
		.disable = _clk_3bit_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable_reg = MXC_CRMAP_AMLPMRD,
		.enable_shift = MXC_CRMAP_AMLPMRD_MLPMD3_OFFSET,
		.enable = _clk_3bit_enable,
		.disable = _clk_3bit_disable,
	},
};
/* wdog_clk stuff end */

/* gpt_clk stuff */
static struct clk gpt_clk = {
	.parent = &ipg_clk,
	.enable_reg = MXC_CRMAP_AMLPMRC,
	.enable_shift = MXC_CRMAP_AMLPMRC_MLPMC4_OFFSET,
	.enable = _clk_3bit_enable,
	.disable = _clk_3bit_disable,
};
/* gpt_clk stuff end */

/* cspi_clk stuff */
static struct clk cspi_clk[] = {
	{
		.id = 0,
		.parent = &ipg_clk,
		.enable_reg = MXC_CRMAP_AMLPMRE2,
		.enable_shift = MXC_CRMAP_AMLPMRE2_MLPME0_OFFSET,
		.enable = _clk_3bit_enable,
		.disable = _clk_3bit_disable,
	}, {
		.id = 1,
		.parent = &ipg_clk,
		.enable_reg = MXC_CRMAP_AMLPMRE1,
		.enable_shift = MXC_CRMAP_AMLPMRE1_MLPME6_OFFSET,
		.enable = _clk_3bit_enable,
		.disable = _clk_3bit_disable,
	},
};
/* cspi_clk stuff end */

#define _REGISTER_CLOCK(d, n, c) \
	{ \
		.dev_id = d, \
		.con_id = n, \
		.clk = &c, \
	},

static struct clk_lookup lookups[] = {
	_REGISTER_CLOCK("imx-uart.0", NULL, uart_clk[0])
	_REGISTER_CLOCK("imx-uart.1", NULL, uart_clk[1])
	_REGISTER_CLOCK("imx-uart.2", NULL, uart_clk[2])
	_REGISTER_CLOCK("mxc-mmc.0", NULL, sdhc_clk[0])
	_REGISTER_CLOCK("mxc-mmc.1", NULL, sdhc_clk[1])
	_REGISTER_CLOCK("mxc-wdt.0", NULL, wdog_clk[0])
	_REGISTER_CLOCK("spi_imx.0", NULL, cspi_clk[0])
	_REGISTER_CLOCK("spi_imx.1", NULL, cspi_clk[1])
};

int __init mxc91231_clocks_init(unsigned long fref)
{
	void __iomem *gpt_base;

	ckih_rate = fref;

	usb_clk.parent = clk_usb_parent(&usb_clk);
	sdhc_clk[0].parent = clk_sdhc_parent(&sdhc_clk[0]);
	sdhc_clk[1].parent = clk_sdhc_parent(&sdhc_clk[1]);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	gpt_base = MXC91231_IO_ADDRESS(MXC91231_GPT1_BASE_ADDR);
	mxc_timer_init(&gpt_clk, gpt_base, MXC91231_INT_GPT);

	return 0;
}
