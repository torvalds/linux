/*
 * Description:
 * Helper functions to support the tegra USB controller
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fsl_devices.h>
#include <linux/io.h>
#include <linux/platform_device.h>

#define USB_SUSP_CTRL		0x400
#define   USB_WAKE_ON_CNNT_EN_DEV	(1 << 3)
#define   USB_WAKE_ON_DISCON_EN_DEV	(1 << 4)
#define   USB_SUSP_CLR		(1 << 5)
#define   UTMIP_RESET			(1 << 11)
#define   USB_SUSP_SET		(1 << 14)

#define USB1_LEGACY_CTRL	0x410
#define   USB1_NO_LEGACY_MODE			(1 << 0)
#define   USB1_VBUS_SENSE_CTL_MASK		(3 << 1)
#define   USB1_VBUS_SENSE_CTL_VBUS_WAKEUP	(0 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD_OR_VBUS_WAKEUP \
						(1 << 1)
#define   USB1_VBUS_SENSE_CTL_AB_SESS_VLD	(2 << 1)
#define   USB1_VBUS_SENSE_CTL_A_SESS_VLD	(3 << 1)

#define UTMIP_PLL_CFG1		0x804
#define   UTMIP_XTAL_FREQ_COUNT(x)		(((x) & 0xfff) << 0)
#define   UTMIP_PLLU_ENABLE_DLY_COUNT(x)	(((x) & 0x1f) << 27)

#define UTMIP_XCVR_CFG0		0x808
#define   UTMIP_XCVR_SETUP(x)			(((x) & 0xf) << 0)
#define   UTMIP_FORCE_PD_POWERDOWN		(1 << 14)
#define   UTMIP_FORCE_PD2_POWERDOWN		(1 << 16)
#define   UTMIP_FORCE_PDZI_POWERDOWN		(1 << 18)

#define UTMIP_HSRX_CFG0		0x810
#define   UTMIP_ELASTIC_LIMIT(x)	(((x) & 0x1f) << 10)
#define   UTMIP_IDLE_WAIT(x)		(((x) & 0x1f) << 15)

#define UTMIP_HSRX_CFG1		0x814
#define   UTMIP_HS_SYNC_START_DLY(x)	(((x) & 0x1f) << 1)

#define UTMIP_TX_CFG0		0x820
#define   UTMIP_FS_PREABMLE_J		(1 << 19)

#define UTMIP_MISC_CFG0		0x824
#define   UTMIP_SUSPEND_EXIT_ON_EDGE	(1 <<22)

#define UTMIP_MISC_CFG1		0x828
#define   UTMIP_PLL_ACTIVE_DLY_COUNT(x)	(((x) & 0x1f) << 18)
#define   UTMIP_PLLU_STABLE_COUNT(x)	(((x) & 0xfff) << 6)

#define UTMIP_DEBOUNCE_CFG0	0x82c
#define   UTMIP_BIAS_DEBOUNCE_A(x)	(((x) & 0xffff) << 0)

#define UTMIP_BAT_CHRG_CFG0	0x830
#define   UTMIP_PD_CHRG			(1 << 0)

#define UTMIP_SPARE_CFG0	0x834
#define   FUSE_SETUP_SEL		(1 << 3);

#define UTMIP_XCVR_CFG1		0x838
#define   UTMIP_FORCE_PDDISC_POWERDOWN	(1 << 0)
#define   UTMIP_FORCE_PDCHRP_POWERDOWN	(1 << 2)
#define   UTMIP_FORCE_PDDR_POWERDOWN	(1 << 4)

static const int udc_freq_table[] =
{
	12000000,
	13000000,
	19200000,
	26000000,
};

static const u8 udc_delay_table[][4] =
{
	/* ENABLE_DLY, STABLE_CNT, ACTIVE_DLY, XTAL_FREQ_CNT */
	{0x02,         0x2F,       0x04,       0x76}, /* 12 MHz */
	{0x02,         0x33,       0x05,       0x7F}, /* 13 MHz */
	{0x03,         0x4B,       0x06,       0xBB}, /* 19.2 MHz */
	{0x04,         0x66,       0x09,       0xFE}, /* 26 Mhz */
};

static const u16 udc_debounce_table[] =
{
	0x7530, /* 12 MHz */
	0x7EF4, /* 13 MHz */
	0xBB80, /* 19.2 MHz */
	0xFDE8, /* 26 MHz */
};


static struct clk *udc_clk;
static struct clk *pll_u_clk;
static void *udc_base;

static unsigned long udc_readl(unsigned long reg)
{
	return readl(udc_base + reg);
}

static void udc_writel(unsigned long val, unsigned long reg)
{
	writel(val, udc_base + reg);
}

static void utmi_phy_init(int freq_sel)
{
	unsigned long val;

	val = udc_readl(USB_SUSP_CTRL);
	val |= UTMIP_RESET;
	udc_writel(val, USB_SUSP_CTRL);

	val = udc_readl(USB1_LEGACY_CTRL);
	val |= USB1_NO_LEGACY_MODE;
	udc_writel(val, USB1_LEGACY_CTRL);

	val = udc_readl(UTMIP_TX_CFG0);
	val |= UTMIP_FS_PREABMLE_J;
	udc_writel(val, UTMIP_TX_CFG0);

	val = udc_readl(UTMIP_HSRX_CFG0);
	val &= ~(UTMIP_IDLE_WAIT(~0) | UTMIP_ELASTIC_LIMIT(~0));
	val |= UTMIP_IDLE_WAIT(17) | UTMIP_ELASTIC_LIMIT(16);
	udc_writel(val, UTMIP_HSRX_CFG0);

	val = udc_readl(UTMIP_HSRX_CFG1);
	val &= ~UTMIP_HS_SYNC_START_DLY(~0);
	val |= UTMIP_HS_SYNC_START_DLY(9);
	udc_writel(val, UTMIP_HSRX_CFG1);

	val = udc_readl(UTMIP_DEBOUNCE_CFG0);
	val &= ~UTMIP_BIAS_DEBOUNCE_A(~0);
	val |= UTMIP_BIAS_DEBOUNCE_A(udc_debounce_table[freq_sel]);
	udc_writel(val, UTMIP_DEBOUNCE_CFG0);

	val = udc_readl(UTMIP_MISC_CFG0);
	val &= ~UTMIP_SUSPEND_EXIT_ON_EDGE;
	udc_writel(val, UTMIP_MISC_CFG0);

	val = udc_readl(UTMIP_MISC_CFG1);
	val &= ~(UTMIP_PLL_ACTIVE_DLY_COUNT(~0) | UTMIP_PLLU_STABLE_COUNT(~0));
	val |= UTMIP_PLL_ACTIVE_DLY_COUNT(udc_delay_table[freq_sel][2]) |
		UTMIP_PLLU_STABLE_COUNT(udc_delay_table[freq_sel][1]);
	udc_writel(val, UTMIP_MISC_CFG1);

	val = udc_readl(UTMIP_PLL_CFG1);
	val &= ~(UTMIP_XTAL_FREQ_COUNT(~0) | UTMIP_PLLU_ENABLE_DLY_COUNT(~0));
	val |= UTMIP_XTAL_FREQ_COUNT(udc_delay_table[freq_sel][3]) |
		UTMIP_PLLU_ENABLE_DLY_COUNT(udc_delay_table[freq_sel][0]);
	udc_writel(val, UTMIP_PLL_CFG1);
}

static void utmi_phy_power_on(void)
{
	unsigned long val;

	val = udc_readl(USB_SUSP_CTRL);
	val &= ~(USB_WAKE_ON_CNNT_EN_DEV | USB_WAKE_ON_DISCON_EN_DEV);
	udc_writel(val, USB_SUSP_CTRL);

	val = udc_readl(UTMIP_XCVR_CFG0);
	val &= ~(UTMIP_FORCE_PD_POWERDOWN | UTMIP_FORCE_PD2_POWERDOWN |
		 UTMIP_FORCE_PDZI_POWERDOWN | UTMIP_XCVR_SETUP(~0));
	val |= UTMIP_XCVR_SETUP(0x8);
	/* TODO: slow rise/fall times in host mode */
	udc_writel(val, UTMIP_XCVR_CFG0);

	val = udc_readl(UTMIP_SPARE_CFG0);
	val |= FUSE_SETUP_SEL;
	udc_writel(val, UTMIP_SPARE_CFG0);

	val = udc_readl(UTMIP_XCVR_CFG1);
	val &= ~(UTMIP_FORCE_PDDISC_POWERDOWN | UTMIP_FORCE_PDCHRP_POWERDOWN |
		 UTMIP_FORCE_PDDR_POWERDOWN);
	udc_writel(val, UTMIP_XCVR_CFG1);

	val = udc_readl(UTMIP_BAT_CHRG_CFG0);
	val &= UTMIP_PD_CHRG;
	udc_writel(val, UTMIP_BAT_CHRG_CFG0);

	/* TODO: usb3 utmip phy needs to be enabled here */

	val = udc_readl(USB_SUSP_CTRL);
	val &= ~UTMIP_RESET;
	udc_writel(val, USB_SUSP_CTRL);

	val = udc_readl(USB1_LEGACY_CTRL);
	val &= ~USB1_VBUS_SENSE_CTL_MASK;
	val |= USB1_VBUS_SENSE_CTL_A_SESS_VLD;
	udc_writel(val, USB1_LEGACY_CTRL);

	val = udc_readl(USB_SUSP_CTRL);
	val &= ~USB_SUSP_SET;
	udc_writel(val, USB_SUSP_CTRL);

	val = udc_readl(USB_SUSP_CTRL);
	val |= USB_SUSP_CLR;
	udc_writel(val, USB_SUSP_CTRL);

	udelay(10);

	val = udc_readl(USB_SUSP_CTRL);
	val &= ~USB_SUSP_CLR;
	udc_writel(val, USB_SUSP_CTRL);
}



int fsl_udc_clk_init(struct platform_device *pdev)
{
	unsigned long parent_rate;
	int freq_sel;
	struct resource *res;
	int err;

	pll_u_clk = clk_get_sys(NULL, "pll_u");
	if (IS_ERR(pll_u_clk)) {
		dev_err(&pdev->dev, "Can't get pll_u clock\n");
		return PTR_ERR(pll_u_clk);
	}

	udc_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc_clk)) {
		dev_err(&pdev->dev, "Can't get udc clock\n");
		err = PTR_ERR(udc_clk);
		goto err0;
	}

	clk_set_rate(pll_u_clk, 480000000);
	clk_enable(pll_u_clk);
	clk_enable(udc_clk);

	parent_rate = clk_get_rate(clk_get_parent(pll_u_clk));

	for (freq_sel = 0; freq_sel < ARRAY_SIZE(udc_freq_table); freq_sel++) {
		if (udc_freq_table[freq_sel] == parent_rate)
			break;
	}

	if (freq_sel == ARRAY_SIZE(udc_freq_table)) {
		dev_err(&pdev->dev, "invalid pll_u parent rate %ld\n",
			parent_rate);
		err = -EINVAL;
		goto err1;
	}

	/* we have to remap the registers ourselves as fsl_udc does not
	 * export them for us.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENXIO;
		goto err1;
	}
	udc_base = ioremap(res->start, resource_size(res));
	if (!udc_base) {
		err = -ENOMEM;
		goto err1;
	}

	utmi_phy_init(freq_sel);
	utmi_phy_power_on();

	return 0;

err1:
	clk_disable(udc_clk);
	clk_disable(pll_u_clk);
	clk_put(udc_clk);

err0:
	clk_put(pll_u_clk);

	return err;
}

void fsl_udc_clk_finalize(struct platform_device *pdev)
{
}

void fsl_udc_clk_release(void)
{
	iounmap(udc_base);

	clk_disable(udc_clk);
	clk_disable(pll_u_clk);
	clk_put(udc_clk);
	clk_put(pll_u_clk);
}

void fsl_udc_clk_suspend(void)
{
}

void fsl_udc_clk_resume(void)
{
}
