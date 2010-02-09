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
#include <linux/platform_device.h>

int fsl_udc_clk_init(struct platform_device *pdev)
{

	return 0;

}

void fsl_udc_clk_finalize(struct platform_device *pdev)
{
}

void fsl_udc_clk_release(void)
{
}

void fsl_udc_clk_suspend(void)
{
}

void fsl_udc_clk_resume(void)
{
}
