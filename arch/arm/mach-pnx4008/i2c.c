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
#include <mach/platform.h>
#include <mach/irqs.h>
#include <mach/i2c.h>

static int set_clock_run(struct platform_device *pdev)
{
	struct clk *clk;
	int retval = 0;

	clk = clk_get(&pdev->dev, NULL);
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
	int retval = 0;

	clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(clk)) {
		clk_set_rate(clk, 0);
		clk_put(clk);
	} else
		retval = -ENOENT;

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
	.calculate_input_freq = calculate_input_freq,
	.set_clock_run = set_clock_run,
	.set_clock_stop = set_clock_stop,
	.adapter = &pnx_adapter0,
};

static struct i2c_pnx_data i2c1_data = {
	.calculate_input_freq = calculate_input_freq,
	.set_clock_run = set_clock_run,
	.set_clock_stop = set_clock_stop,
	.adapter = &pnx_adapter1,
};

static struct i2c_pnx_data i2c2_data = {
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
