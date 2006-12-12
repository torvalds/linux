/*
 * I2C initialization for PNX4008.
 *
 * Author: Vitaly Wool <vitalywool@gmail.com>
 *
 * 2005-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/i2c-pnx.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <asm/arch/platform.h>
#include <asm/arch/i2c.h>

static int set_clock_run(struct platform_device *pdev)
{
	struct clk *clk;
	char name[10];
	int retval = 0;

	snprintf(name, 10, "i2c%d_ck", pdev->id);
	clk = clk_get(&pdev->dev, name);
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 1);
		clk_put(clk);
	} else
		retval = -ENOENT;

	return retval;
}

static int set_clock_stop(struct platform_device *pdev)
{
	struct clk *clk;
	char name[10];
	int retval = 0;

	snprintf(name, 10, "i2c%d_ck", pdev->id);
	clk = clk_get(&pdev->dev, name);
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 0);
		clk_put(clk);
	} else
		retval = -ENOENT;

	return retval;
}

static int i2c_pnx_suspend(struct platform_device *pdev, pm_message_t state)
{
	int retval = 0;
#ifdef CONFIG_PM
	retval = set_clock_run(pdev);
#endif
	return retval;
}

static int i2c_pnx_resume(struct platform_device *pdev)
{
	int retval = 0;
#ifdef CONFIG_PM
	retval = set_clock_run(pdev);
#endif
	return retval;
}

static u32 calculate_input_freq(struct platform_device *pdev)
{
	return HCLK_MHZ;
}


static struct i2c_pnx_algo_data pnx_algo_data0 = {
	.base = PNX4008_I2C1_BASE,
	.irq = I2C_1_INT,
};

static struct i2c_pnx_algo_data pnx_algo_data1 = {
	.base = PNX4008_I2C2_BASE,
	.irq = I2C_2_INT,
};

static struct i2c_pnx_algo_data pnx_algo_data2 = {
	.base = (PNX4008_USB_CONFIG_BASE + 0x300),
	.irq = USB_I2C_INT,
};

static struct i2c_adapter pnx_adapter0 = {
	.name = I2C_CHIP_NAME "0",
	.algo_data = &pnx_algo_data0,
};
static struct i2c_adapter pnx_adapter1 = {
	.name = I2C_CHIP_NAME "1",
	.algo_data = &pnx_algo_data1,
};

static struct i2c_adapter pnx_adapter2 = {
	.name = "USB-I2C",
	.algo_data = &pnx_algo_data2,
};

static struct i2c_pnx_data i2c0_data = {
	.suspend = i2c_pnx_suspend,
	.resume = i2c_pnx_resume,
	.calculate_input_freq = calculate_input_freq,
	.set_clock_run = set_clock_run,
	.set_clock_stop = set_clock_stop,
	.adapter = &pnx_adapter0,
};

static struct i2c_pnx_data i2c1_data = {
	.suspend = i2c_pnx_suspend,
	.resume = i2c_pnx_resume,
	.calculate_input_freq = calculate_input_freq,
	.set_clock_run = set_clock_run,
	.set_clock_stop = set_clock_stop,
	.adapter = &pnx_adapter1,
};

static struct i2c_pnx_data i2c2_data = {
	.suspend = i2c_pnx_suspend,
	.resume = i2c_pnx_resume,
	.calculate_input_freq = calculate_input_freq,
	.set_clock_run = set_clock_run,
	.set_clock_stop = set_clock_stop,
	.adapter = &pnx_adapter2,
};

static struct platform_device i2c0_device = {
	.name = "pnx-i2c",
	.id = 0,
	.dev = {
		.platform_data = &i2c0_data,
	},
};

static struct platform_device i2c1_device = {
	.name = "pnx-i2c",
	.id = 1,
	.dev = {
		.platform_data = &i2c1_data,
	},
};

static struct platform_device i2c2_device = {
	.name = "pnx-i2c",
	.id = 2,
	.dev = {
		.platform_data = &i2c2_data,
	},
};

static struct platform_device *devices[] __initdata = {
	&i2c0_device,
	&i2c1_device,
	&i2c2_device,
};

void __init pnx4008_register_i2c_devices(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
}
