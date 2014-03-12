
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

#include "stmmac.h"


struct gmac_phy_data {
	int power_io;
	int power_io_enable;
};
struct gmac_phy_data g_gmac_phy_data;

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

// RK3288_GRF_SOC_CON1
#define GMAC_PHY_INTF_SEL_RGMII ((0x01C0 << 16) | (0x0040))
#define GMAC_PHY_INTF_SEL_RMII  ((0x01C0 << 16) | (0x0100))
#define GMAC_FLOW_CTRL          ((0x0200 << 16) | (0x0200))
#define GMAC_FLOW_CTRL_CLR      ((0x0200 << 16) | (0x0000))
#define GMAC_SPEED_10M          ((0x0400 << 16) | (0x0400))
#define GMAC_SPEED_100M         ((0x0400 << 16) | (0x0000))
#define GMAC_RMII_CLK_25M       ((0x0800 << 16) | (0x0800))
#define GMAC_RMII_CLK_2_5M      ((0x0800 << 16) | (0x0000))
#define GMAC_CLK_125M           ((0x3000 << 16) | (0x0000))
#define GMAC_CLK_25M            ((0x3000 << 16) | (0x3000))
#define GMAC_CLK_2_5M           ((0x3000 << 16) | (0x2000))
#define GMAC_RMII_MODE          ((0x4000 << 16) | (0x4000))

// RK3288_GRF_SOC_CON3
#define GMAC_CLK_TX_DL_CFG(val) ((0x007F << 16) | (val))           // 7bit
#define GMAC_CLK_RX_DL_CFG(val) ((0x007F << 16) | (val<<7))        // 7bit
#define GMAC_TXCLK_DLY_ENABLE   ((0x4000 << 16) | (0x4000)) 
#define GMAC_TXCLK_DLY_DISABLE  ((0x4000 << 16) | (0x0000))
#define GMAC_RXCLK_DLY_ENABLE   ((0x8000 << 16) | (0x8000)) 
#define GMAC_RXCLK_DLY_DISABLE  ((0x8000 << 16) | (0x0000)) 

static int rk_gmac_register_init(void)
{
	printk("enter %s \n",__func__);
	
	//select rmii
	grf_writel(GMAC_PHY_INTF_SEL_RMII, RK3288_GRF_SOC_CON1);

	return 0;
}

static int rk_gmac_power_control(int enable)
{
	struct gmac_phy_data *pdata = &g_gmac_phy_data;
	
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
	return 0;
}

static int rk_gmac_io_init(struct device *device)
{
	printk("enter %s \n",__func__);

	// iomux
	
	// clock enable
	
	// phy power on
	rk_gmac_power_control(1);

	return 0;
}

static int rk_gmac_io_deinit(struct device *device)
{
	printk("enter %s \n",__func__);
	
	// clock disable
	
	// phy power down
	rk_gmac_power_control(0);
	
	return 0;
}

static int rk_gmac_speed_switch(int speed)
{
	printk("%s: speed = %d\n", __func__, speed);

	return 0;
}

struct rk_gmac_platform_data rk_board_gmac_data = {
	.gmac_register_init = rk_gmac_register_init,
	.gmac_io_init = rk_gmac_io_init,
	.gmac_io_deinit = rk_gmac_io_deinit,
	.gmac_speed_switch = rk_gmac_speed_switch,
};

static int gmac_phy_probe(struct platform_device *pdev)
{
	struct gmac_phy_data *pdata = pdev->dev.platform_data;
	enum of_gpio_flags flags;
	int ret = 0, err;
	struct device_node *node = pdev->dev.of_node;

    printk("enter %s \n",__func__);
	if (!pdata) {
		pdata = &g_gmac_phy_data;

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
	/*err = gpio_request(pdata->power_io, "gmac_phy_power");
	if (err) {
		printk("%s: Request gmac phy power pin failed.\n", __func__);
		return -EINVAL;
	}*/	
	
	gpio_direction_output(pdata->power_io, !pdata->power_io_enable);
	gpio_set_value(pdata->power_io, !pdata->power_io_enable);

	return ret;
}

static int gmac_phy_remove(struct platform_device *pdev)
{
	struct gmac_phy_data *pdata = pdev->dev.platform_data;
	
	printk("enter %s \n",__func__);
	if (gpio_is_valid(pdata->power_io))
		gpio_free(pdata->power_io);
		
	return 0;
}

static struct of_device_id gmac_phy_of_match[] = {
	{ .compatible = "rockchip,gmac-phy" },
	{ }
};
MODULE_DEVICE_TABLE(of, gmac_phy_of_match);

static struct platform_driver gmac_phy_driver = {
	.driver		= {
		.name		= "rockchip,gmac-phy",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(gmac_phy_of_match),
	},
	.probe		= gmac_phy_probe,
	.remove		= gmac_phy_remove,
};

module_platform_driver(gmac_phy_driver);

MODULE_DESCRIPTION("GMAC PHY Power Driver");
MODULE_LICENSE("GPL");
