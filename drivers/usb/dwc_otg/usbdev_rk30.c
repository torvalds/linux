#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include "usbdev_rk.h"
#include "dwc_otg_regs.h"

static struct dwc_otg_control_usb *control_usb;

int dwc_otg_check_dpdm(void)
{
	int bus_status = 0;
	return bus_status;
}

EXPORT_SYMBOL(dwc_otg_check_dpdm);

#ifdef CONFIG_USB20_OTG

void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
	unsigned int * otg_phy_con1 = (control_usb->grf_uoc1_base + 0x8);
	unsigned int * otg_phy_con2 = (control_usb->grf_uoc1_base + 0xc);
	*otg_phy_con1 = (0x01<<2)|((0x01<<2)<<16);     //enable soft control
	*otg_phy_con2 = 0x2A|(0x3F<<16);               // enter suspend
#endif
	/* usb phy config init
	 * usb phy enter usb mode */
	unsigned int * otg_phy_con3 = (control_usb->grf_uoc0_base);
	*otg_phy_con3 = (0x0300 << 16);

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	gpio_direction_output(control_usb->otg_gpios->gpio, 0);
}

void usb20otg_phy_suspend(void* pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata=pdata;
	unsigned int * otg_phy_con1 = (control_usb->grf_uoc0_base + 0x8);
	unsigned int * otg_phy_con2 = (control_usb->grf_uoc0_base + 0xc);

	if(suspend){
		*otg_phy_con1 = (0x01<<2)|((0x01<<2)<<16);	//enable soft control
		*otg_phy_con2 = 0x2A|(0x3F<<16);		// enter suspend
		usbpdata->phy_status = 1;
	}else{
		*otg_phy_con1 = ((0x01<<2)<<16);		// exit suspend.
		usbpdata->phy_status = 0;
	}
}

void usb20otg_soft_reset(void)
{
}

void usb20otg_clock_init(void* pdata)
{
	/*
	struct dwc_otg_platform_data *usbpdata=pdata;
	struct clk* ahbclk,*phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_otg0");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_otg0\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "otgphy0");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get otgphy0\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;*/
}

void usb20otg_clock_enable(void* pdata, int enable)
{
	/*
	struct dwc_otg_platform_data *usbpdata=pdata;

	if(enable){
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
	}else{
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
	}
	*/
}

int usb20otg_get_status(int id)
{
	int ret = -1;
	unsigned int usbgrf_status = *(unsigned int*)(control_usb->grf_soc_status0);

	switch(id){
		case USB_STATUS_BVABLID:
			// bvalid in grf
			ret = (usbgrf_status &(1<<10));
			break;
		case USB_STATUS_DPDM:
			// dpdm in grf
			ret = (usbgrf_status &(3<<11));
			break;
		case USB_STATUS_ID:
			// id in grf
			ret = (usbgrf_status &(1<<13));
			break;
		default:
			break;
	}

	return ret;
}

#ifdef CONFIG_RK_USB_UART
void dwc_otg_uart_mode(void* pdata, int enter_usb_uart_mode)
{
	unsigned int * otg_phy_con1 = (unsigned int*)(control_usb->grf_uoc0_base);

	if(1 == enter_usb_uart_mode){
		/* enter uart mode
		 * note: can't disable otg here! If otg disable, the ID change
		 * interrupt can't be triggered when otg cable connect without
		 * device.At the same time, uart can't be used normally
		 */
		*otg_phy_con1 = (0x0300 | (0x0300 << 16));	//bypass dm
	}else if(0 == enter_usb_uart_mode){
		/* enter usb mode */
		*otg_phy_con1 = (0x0300 << 16);			//bypass dm disable
	}
}
#endif

void usb20otg_power_enable(int enable)
{
	if(0 == enable){//disable otg_drv power
		gpio_set_value(control_usb->otg_gpios->gpio, 0);
	}else if(1 == enable){//enable otg_drv power
		gpio_set_value(control_usb->otg_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20otg_pdata = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.busclk = NULL,
	.phy_status = 0,
	.hw_init=usb20otg_hw_init,
	.phy_suspend=usb20otg_phy_suspend,
	.soft_reset=usb20otg_soft_reset,
	.clock_init=usb20otg_clock_init,
	.clock_enable=usb20otg_clock_enable,
	.get_status=usb20otg_get_status,
	.power_enable=usb20otg_power_enable,
#ifdef CONFIG_RK_USB_UART
	.dwc_otg_uart_mode=dwc_otg_uart_mode,
#endif
};

#endif

#ifdef CONFIG_USB20_HOST
void usb20host_hw_init(void)
{
	/* usb phy config init */

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	gpio_direction_output(control_usb->host_gpios->gpio, 1);

}

void usb20host_phy_suspend(void* pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata=pdata;
	unsigned int * otg_phy_con1 = (unsigned int*)(control_usb->grf_uoc1_base + 0x8);
	unsigned int * otg_phy_con2 = (unsigned int*)(control_usb->grf_uoc1_base + 0xc);

	if(suspend){
		*otg_phy_con1 =  (0x01<<2)|((0x01<<2)<<16);	// enable soft control
		*otg_phy_con2 =  0x2A|(0x3F<<16);		// enter suspend
		usbpdata->phy_status = 1;
	}else{
		*otg_phy_con1 = ((0x01<<2)<<16);		// exit suspend.
		usbpdata->phy_status = 0;
	}
}

void usb20host_soft_reset(void)
{
}

void usb20host_clock_init(void* pdata)
{
	/*
	struct dwc_otg_platform_data *usbpdata=pdata;
	struct clk* ahbclk,*phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_otg1");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_otg1\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "otgphy1");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get otgphy1\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;*/
}

void usb20host_clock_enable(void* pdata, int enable)
{
	/*
	struct dwc_otg_platform_data *usbpdata=pdata;

	if(enable){
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
	}else{
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
	}*/
}

int usb20host_get_status(int id)
{
	int ret = -1;
	unsigned int usbgrf_status = *(unsigned int*)(control_usb->grf_soc_status0);

	switch(id){
		case USB_STATUS_BVABLID:
			// bvalid in grf
			ret = (usbgrf_status &(1<<17));
			break;
		case USB_STATUS_DPDM:
			// dpdm in grf
			ret = (usbgrf_status &(3<<18));
			break;
		case USB_STATUS_ID:
			// id in grf
			ret = (usbgrf_status &(1<<20));
			break;
		default:
			break;
	}

	return ret;
}

void usb20host_power_enable(int enable)
{
	if(0 == enable){//disable host_drv power
		//do not disable power in default
	}else if(1 == enable){//enable host_drv power
		gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20host_pdata = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.busclk = NULL,
	.phy_status = 0,
	.hw_init=usb20host_hw_init,
	.phy_suspend=usb20host_phy_suspend,
	.soft_reset=usb20host_soft_reset,
	.clock_init=usb20host_clock_init,
	.clock_enable=usb20host_clock_enable,
	.get_status=usb20host_get_status,
	.power_enable=usb20host_power_enable,
};
#endif

static int dwc_otg_control_usb_probe(struct platform_device *pdev)
{
	struct resource	*res;
	int gpio, err;
	struct device_node *np = pdev->dev.of_node;

	control_usb = devm_kzalloc(&pdev->dev, sizeof(*control_usb),GFP_KERNEL);
	if (!control_usb) {
		dev_err(&pdev->dev, "unable to alloc memory for control usb\n");
		return -ENOMEM;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"GRF_SOC_STATUS0");
	control_usb->grf_soc_status0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(control_usb->grf_soc_status0))
		return PTR_ERR(control_usb->grf_soc_status0);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"GRF_UOC0_BASE");
	control_usb->grf_uoc0_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(control_usb->grf_uoc0_base))
		return PTR_ERR(control_usb->grf_uoc0_base);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						"GRF_UOC1_BASE");
	control_usb->grf_uoc1_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(control_usb->grf_uoc1_base))
		return PTR_ERR(control_usb->grf_uoc1_base);

	control_usb->host_gpios = devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);

	gpio =  of_get_named_gpio(np, "gpios", 0);
	if(!gpio_is_valid(gpio)){
		dev_err(&pdev->dev, "invalid host gpio%d\n", gpio);
		return -EINVAL;
	}
	control_usb->host_gpios->gpio = gpio;
	err = devm_gpio_request(&pdev->dev, gpio, "host_drv_gpio");
	if (err) {
		dev_err(&pdev->dev,
			"failed to request GPIO%d for host_drv\n",
			gpio);
		return err;
	}

	control_usb->otg_gpios = devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);

	gpio =  of_get_named_gpio(np, "gpios", 1);
	if(!gpio_is_valid(gpio)){
		dev_err(&pdev->dev, "invalid otg gpio%d\n", gpio);
		return -EINVAL;
	}
	control_usb->otg_gpios->gpio = gpio;
	err = devm_gpio_request(&pdev->dev, gpio, "otg_drv_gpio");
	if (err) {
		dev_err(&pdev->dev,
			"failed to request GPIO%d for otg_drv\n",
			gpio);
		return err;
	}

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id dwc_otg_control_usb_id_table[] = {
	{ .compatible = "rockchip,dwc-control-usb" },
	{}
};
MODULE_DEVICE_TABLE(of, dwc_otg_control_usb_id_table);
#endif

static struct platform_driver dwc_otg_control_usb_driver = {
	.probe		= dwc_otg_control_usb_probe,
	.driver		= {
		.name	= "dwc-control-usb",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(dwc_otg_control_usb_id_table),
	},
};

static int __init dwc_otg_control_usb_init(void)
{
	return platform_driver_register(&dwc_otg_control_usb_driver);
}
subsys_initcall(dwc_otg_control_usb_init);

static void __exit dwc_otg_control_usb_exit(void)
{
	platform_driver_unregister(&dwc_otg_control_usb_driver);
}

module_exit(dwc_otg_control_usb_exit);
MODULE_ALIAS("platform: dwc_control_usb");
MODULE_AUTHOR("RockChip Inc.");
MODULE_DESCRIPTION("RockChip Control Module USB Driver");
MODULE_LICENSE("GPL v2");
