/*
 * Description:
 * Helper functions to support the tegra USB controller
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/fsl_devices.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <mach/usb_phy.h>

static struct tegra_usb_phy *phy;
static struct clk *udc_clk;
static struct clk *emc_clk;
static void *udc_base;

int fsl_udc_clk_init(struct platform_device *pdev)
{
	struct resource *res;
	int err;
	int instance;
	struct fsl_usb2_platform_data *pdata = pdev->dev.platform_data;


	udc_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(udc_clk)) {
		dev_err(&pdev->dev, "Can't get udc clock\n");
		return PTR_ERR(udc_clk);
	}

	clk_enable(udc_clk);

	emc_clk = clk_get(&pdev->dev, "emc");
	if (IS_ERR(emc_clk)) {
		dev_err(&pdev->dev, "Can't get emc clock\n");
		err = PTR_ERR(emc_clk);
		goto err_emc;
	}

	clk_enable(emc_clk);
	clk_set_rate(emc_clk, 400000000);

	/* we have to remap the registers ourselves as fsl_udc does not
	 * export them for us.
	 */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -ENXIO;
		goto err0;
	}
	udc_base = ioremap(res->start, resource_size(res));
	if (!udc_base) {
		err = -ENOMEM;
		goto err0;
	}

	instance = pdev->id;
	if (instance == -1)
		instance = 0;

	phy = tegra_usb_phy_open(instance, udc_base, pdata->phy_config,
						TEGRA_USB_PHY_MODE_DEVICE);
	if (IS_ERR(phy)) {
		dev_err(&pdev->dev, "Can't open phy\n");
		err = PTR_ERR(phy);
		goto err1;
	}

	tegra_usb_phy_power_on(phy);

	return 0;
err1:
	iounmap(udc_base);
err0:
	clk_disable(emc_clk);
	clk_put(emc_clk);
err_emc:
	clk_disable(udc_clk);
	clk_put(udc_clk);
	return err;
}

void fsl_udc_clk_finalize(struct platform_device *pdev)
{
}

void fsl_udc_clk_release(void)
{
	tegra_usb_phy_close(phy);

	iounmap(udc_base);

	clk_disable(udc_clk);
	clk_put(udc_clk);

	clk_disable(emc_clk);
	clk_put(emc_clk);
}

void fsl_udc_clk_suspend(void)
{
	tegra_usb_phy_power_off(phy);
	clk_disable(udc_clk);
	clk_disable(emc_clk);
}

void fsl_udc_clk_resume(void)
{
	clk_enable(emc_clk);
	clk_enable(udc_clk);
	tegra_usb_phy_power_on(phy);
}
