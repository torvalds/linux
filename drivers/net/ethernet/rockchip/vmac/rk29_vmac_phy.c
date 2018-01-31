/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/clk.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wakelock.h>
#include <linux/version.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <asm/irq.h>
#include <linux/interrupt.h>
#include <linux/completion.h>

#include <linux/rockchip/iomap.h>
#include <linux/rockchip/grf.h>

#include "rk29_vmac.h"
#if 0
struct vmac_phy_data {
	int power_io;
	int power_io_enable;
};
struct vmac_phy_data g_vmac_phy_data;
#endif

#define grf_readl(offset)       readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)   do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

static int rk30_vmac_register_set(void)
{
	//config rk30 vmac as rmii
	grf_writel((0<<8) | ((1<<8)<<16), RK3036_GRF_SOC_CON0);
	//newrev_en
	grf_writel((1<<15) | ((1<<15)<<16), RK3036_GRF_SOC_CON0);
	return 0;
}

static int rk30_rmii_io_init(void)
{
	printk("enter %s \n",__func__);

	//rk3188 gpio3 and sdio drive strength , 
	//grf_writel((0x0f<<16)|0x0f, RK3188_GRF_IO_CON3);

	return 0;
}

static int rk30_rmii_io_deinit(void)
{
	//phy power down
	printk("enter %s \n",__func__);
	return 0;
}

static int rk30_rmii_power_control(int enable)
{
#if 0
	struct vmac_phy_data *pdata = &g_vmac_phy_data;
	
	printk("enter %s ,enable = %d \n",__func__,enable);
	if (enable) {
		if (gpio_is_valid(pdata->power_io)) {
			gpio_direction_output(pdata->power_io, pdata->power_io_enable);
			gpio_set_value(pdata->power_io, pdata->power_io_enable);
		}	
	}else {
		if (gpio_is_valid(pdata->power_io)) {
			gpio_direction_output(pdata->power_io, !pdata->power_io_enable);
			gpio_set_value(pdata->power_io, !pdata->power_io_enable);
		}
	}
#endif
	return 0;
}

static int rk29_vmac_speed_switch(int speed)
{
	//printk("%s: speed = %d\n", __func__, speed);
	if (10 == speed) {
	    grf_writel((0<<9) | ((1<<9)<<16), RK3036_GRF_SOC_CON0);
	} else {
	    grf_writel((1<<9) | ((1<<9)<<16), RK3036_GRF_SOC_CON0);
	}
	return 0;
}

struct rk29_vmac_platform_data board_vmac_data = {
	.vmac_register_set = rk30_vmac_register_set,
	.rmii_io_init = rk30_rmii_io_init,
	.rmii_io_deinit = rk30_rmii_io_deinit,
	.rmii_power_control = rk30_rmii_power_control,
	.rmii_speed_switch = rk29_vmac_speed_switch,
};
#if 0
static int vmac_phy_probe(struct platform_device *pdev)
{
	struct vmac_phy_data *pdata = pdev->dev.platform_data;
	enum of_gpio_flags flags;
	int ret = 0, err;
	struct device_node *node = pdev->dev.of_node;

    printk("enter %s \n",__func__);
	if (!pdata) {
		pdata = &g_vmac_phy_data;

		pdata->power_io = of_get_named_gpio_flags(node, "power-gpios", 0, &flags);
		if (!gpio_is_valid(pdata->power_io)) {
			printk("%s: Get power-gpios failed.\n", __func__);
			return -EINVAL;
		}
		
		if(flags & OF_GPIO_ACTIVE_LOW)
			pdata->power_io_enable = 0;
		else
			pdata->power_io_enable = 1;
	}
	
	// disable power
	/*err = gpio_request(pdata->power_io, "vmac_phy_power");
	if (err) {
		printk("%s: Request vmac phy power pin failed.\n", __func__);
		return -EINVAL;
	}*/	
	
	gpio_direction_output(pdata->power_io, !pdata->power_io_enable);
	gpio_set_value(pdata->power_io, !pdata->power_io_enable);

	return ret;
}

static int vmac_phy_remove(struct platform_device *pdev)
{
	struct vmac_phy_data *pdata = pdev->dev.platform_data;
	
	printk("enter %s \n",__func__);
	if (gpio_is_valid(pdata->power_io))
		gpio_free(pdata->power_io);
		
	return 0;
}

static struct of_device_id vmac_phy_of_match[] = {
	{ .compatible = "rockchip,vmac-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, vmac_phy_of_match);

static struct platform_driver vmac_phy_driver = {
	.driver		= {
		.name		= "rockchip,vmac-phy",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(vmac_phy_of_match),
	},
	.probe		= vmac_phy_probe,
	.remove		= vmac_phy_remove,
};

module_platform_driver(vmac_phy_driver);

MODULE_DESCRIPTION("VMAC PHY Power Driver");
MODULE_LICENSE("GPL");
#endif
