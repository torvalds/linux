
#include "usbdev_rk.h"
#include "usbdev_grf_regs.h"
#include "dwc_otg_regs.h"

static struct dwc_otg_control_usb *control_usb;

#ifdef CONFIG_USB20_OTG

static void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
	/* enable soft control */
	control_usb->grf_uoc1_base->CON2 = (0x01 << 2) | ((0x01 << 2) << 16);
	/* enter suspend */
	control_usb->grf_uoc1_base->CON3 = 0x2A | (0x3F << 16);
#endif
	/* usb phy config init
	 * usb phy enter usb mode */
	control_usb->grf_uoc0_base->CON0 = (0x0300 << 16);

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (gpio_get_value(control_usb->otg_gpios->gpio))
		gpio_set_value(control_usb->otg_gpios->gpio, 0);
}

static void usb20otg_phy_suspend(void *pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (suspend) {
		/* enable soft control */
		control_usb->grf_uoc0_base->CON2 =
		    (0x01 << 2) | ((0x01 << 2) << 16);
		/* enter suspend */
		control_usb->grf_uoc0_base->CON3 = 0x2A | (0x3F << 16);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		control_usb->grf_uoc0_base->CON2 = ((0x01 << 2) << 16);
		usbpdata->phy_status = 0;
	}
}

static void usb20otg_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
}

static void usb20otg_clock_init(void *pdata)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_usb0");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_usb0\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "clk_usbphy0");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get clk_usbphy0\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}

static void usb20otg_clock_enable(void *pdata, int enable)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (enable) {
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
	} else {
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
	}
}

static int usb20otg_get_status(int id)
{
	int ret = -1;

	switch (id) {
	case USB_STATUS_BVABLID:
		/* bvalid in grf */
		ret = control_usb->grf_soc_status0_rk3188->otg_bvalid;
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = control_usb->grf_soc_status0_rk3188->otg_linestate;
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = control_usb->grf_soc_status0_rk3188->otg_iddig;
		break;
	case USB_CHIP_ID:
		ret = control_usb->chip_id;
		break;
	case USB_REMOTE_WAKEUP:
		ret = control_usb->remote_wakeup;
		break;
	case USB_IRQ_WAKEUP:
		ret = control_usb->usb_irq_wakeup;
		break;
	default:
		break;
	}

	return ret;
}

#ifdef CONFIG_RK_USB_UART
static void dwc_otg_uart_mode(void *pdata, int enter_usb_uart_mode)
{
	if (1 == enter_usb_uart_mode) {
		/* enter uart mode
		 * note: can't disable otg here! If otg disable, the ID change
		 * interrupt can't be triggered when otg cable connect without
		 * device.At the same time, uart can't be used normally
		 */
		/* bypass dm, enter uart mode */
		control_usb->grf_uoc0_base->CON0 = (0x0300 | (0x0300 << 16));
	} else if (0 == enter_usb_uart_mode) {
		/* enter usb mode */
		control_usb->grf_uoc0_base->CON0 = (0x0300 << 16);
	}
}
#endif

static void usb20otg_power_enable(int enable)
{
	if (0 == enable) {
		/* disable otg_drv power */
		gpio_set_value(control_usb->otg_gpios->gpio, 0);
	} else if (1 == enable) {
		/* enable otg_drv power */
		gpio_set_value(control_usb->otg_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20otg_pdata_rk3188 = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.busclk = NULL,
	.phy_status = 0,
	.hw_init = usb20otg_hw_init,
	.phy_suspend = usb20otg_phy_suspend,
	.soft_reset = usb20otg_soft_reset,
	.clock_init = usb20otg_clock_init,
	.clock_enable = usb20otg_clock_enable,
	.get_status = usb20otg_get_status,
	.power_enable = usb20otg_power_enable,
#ifdef CONFIG_RK_USB_UART
	.dwc_otg_uart_mode = dwc_otg_uart_mode,
#endif
	.bc_detect_cb = usb20otg_battery_charger_detect_cb,
};

#endif

#ifdef CONFIG_USB20_HOST
static void usb20host_hw_init(void)
{
	/* usb phy config init */

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (!gpio_get_value(control_usb->host_gpios->gpio))
		gpio_set_value(control_usb->host_gpios->gpio, 1);
}

static void usb20host_phy_suspend(void *pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (suspend) {
		/* enable soft control */
		control_usb->grf_uoc1_base->CON2 =
		    (0x01 << 2) | ((0x01 << 2) << 16);
		/* enter suspend */
		control_usb->grf_uoc1_base->CON3 = 0x2A | (0x3F << 16);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		control_usb->grf_uoc1_base->CON2 = ((0x01 << 2) << 16);
		usbpdata->phy_status = 0;
	}
}

static void usb20host_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
}

static void usb20host_clock_init(void *pdata)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_usb1");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_usb1\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "clk_usbphy1");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get clk_usbphy1\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}

static void usb20host_clock_enable(void *pdata, int enable)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (enable) {
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
	} else {
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
	}
}

static int usb20host_get_status(int id)
{
	int ret = -1;

	switch (id) {
	case USB_STATUS_BVABLID:
		/* bvalid in grf */
		ret = control_usb->grf_soc_status0_rk3188->uhost_bvalid;
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = control_usb->grf_soc_status0_rk3188->uhost_linestate;
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = control_usb->grf_soc_status0_rk3188->uhost_iddig;
		break;
	case USB_CHIP_ID:
		ret = control_usb->chip_id;
		break;
	case USB_REMOTE_WAKEUP:
		ret = control_usb->remote_wakeup;
		break;
	case USB_IRQ_WAKEUP:
		ret = control_usb->usb_irq_wakeup;
		break;
	default:
		break;
	}

	return ret;
}

static void usb20host_power_enable(int enable)
{
	if (0 == enable) {
		/* disable host_drv power */
		/* do not disable power in default */
	} else if (1 == enable) {
		/* enable host_drv power */
		gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20host_pdata_rk3188 = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.busclk = NULL,
	.phy_status = 0,
	.hw_init = usb20host_hw_init,
	.phy_suspend = usb20host_phy_suspend,
	.soft_reset = usb20host_soft_reset,
	.clock_init = usb20host_clock_init,
	.clock_enable = usb20host_clock_enable,
	.get_status = usb20host_get_status,
	.power_enable = usb20host_power_enable,
};
#endif

#ifdef CONFIG_USB_EHCI_RKHSIC
static void rk_hsic_hw_init(void)
{
	/* usb phy config init
	 * hsic phy config init, set hsicphy_txsrtune */
	control_usb->grf_uoc2_base->CON0 = ((0xf << 6) << 16) | (0xf << 6);

	/* other haredware init
	 * set common_on, in suspend mode, otg/host PLL blocks remain powered
	 * for RK3168 set control_usb->grf_uoc0_base->CON0 = (1<<16)|0;
	 * for Rk3188 set control_usb->grf_uoc1_base->CON0 = (1<<16)|0;
	 */
	control_usb->grf_uoc1_base->CON0 = (1 << 16) | 0;

	/* change INCR to INCR16 or INCR8(beats less than 16)
	 * or INCR4(beats less than 8) or SINGLE(beats less than 4)
	 */
	control_usb->grf_uoc3_base->CON0 = 0x00ff00bc;
}

static void rk_hsic_clock_init(void *pdata)
{
	/* By default, hsicphy_480m's parent is otg phy 480MHz clk
	 * rk3188 must use host phy 480MHz clk, because if otg bypass
	 * to uart mode, otg phy 480MHz clk will be closed automatically
	 */
	struct rkehci_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk480m_hsic, *phyclk12m_hsic, *phyclk_usbphy1;

	phyclk480m_hsic = devm_clk_get(usbpdata->dev, "hsicphy_480m");
	if (IS_ERR(phyclk480m_hsic)) {
		dev_err(usbpdata->dev, "Failed to get hsicphy_480m\n");
		return;
	}

	phyclk12m_hsic = devm_clk_get(usbpdata->dev, "hsicphy_12m");
	if (IS_ERR(phyclk12m_hsic)) {
		dev_err(usbpdata->dev, "Failed to get hsicphy_12m\n");
		return;
	}

	phyclk_usbphy1 = devm_clk_get(usbpdata->dev, "hsic_usbphy1");
	if (IS_ERR(phyclk_usbphy1)) {
		dev_err(usbpdata->dev, "Failed to get hsic_usbphy1\n");
		return;
	}

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_hsic");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_hsic\n");
		return;
	}

	clk_set_parent(phyclk480m_hsic, phyclk_usbphy1);

	usbpdata->hclk_hsic = ahbclk;
	usbpdata->hsic_phy_480m = phyclk480m_hsic;
	usbpdata->hsic_phy_12m = phyclk12m_hsic;
}

static void rk_hsic_clock_enable(void *pdata, int enable)
{
	struct rkehci_platform_data *usbpdata = pdata;

	if (enable == usbpdata->clk_status)
		return;
	if (enable) {
		clk_prepare_enable(usbpdata->hclk_hsic);
		clk_prepare_enable(usbpdata->hsic_phy_480m);
		clk_prepare_enable(usbpdata->hsic_phy_12m);
		usbpdata->clk_status = 1;
	} else {
		clk_disable_unprepare(usbpdata->hclk_hsic);
		clk_disable_unprepare(usbpdata->hsic_phy_480m);
		clk_disable_unprepare(usbpdata->hsic_phy_12m);
		usbpdata->clk_status = 0;
	}
}

static void rk_hsic_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{

}

struct rkehci_platform_data rkhsic_pdata_rk3188 = {
	.hclk_hsic = NULL,
	.hsic_phy_12m = NULL,
	.hsic_phy_480m = NULL,
	.clk_status = -1,
	.hw_init = rk_hsic_hw_init,
	.clock_init = rk_hsic_clock_init,
	.clock_enable = rk_hsic_clock_enable,
	.soft_reset = rk_hsic_soft_reset,
};
#endif

#define WAKE_LOCK_TIMEOUT (HZ * 10)

static inline void do_wakeup(struct work_struct *work)
{
	rk_send_wakeup_key();	/* wake up the system */
}

/********** handler for bvalid irq **********/
static irqreturn_t bvalid_irq_handler(int irq, void *dev_id)
{
	/* clear irq */
	control_usb->grf_uoc0_base->CON3 = (1 << 31) | (1 << 15);

#ifdef CONFIG_RK_USB_UART
	/* usb otg dp/dm switch to usb phy */
	dwc_otg_uart_mode(NULL, PHY_USB_MODE);
#endif

	if (control_usb->usb_irq_wakeup) {
		wake_lock_timeout(&control_usb->usb_wakelock,
				  WAKE_LOCK_TIMEOUT);
		schedule_delayed_work(&control_usb->usb_det_wakeup_work,
				      HZ / 10);
	}

	return IRQ_HANDLED;
}

/************* register bvalid irq **************/
static int otg_irq_detect_init(struct platform_device *pdev)
{
	int ret = 0;
	int irq = 0;

	if (control_usb->usb_irq_wakeup) {
		wake_lock_init(&control_usb->usb_wakelock, WAKE_LOCK_SUSPEND,
			       "usb_detect");
		INIT_DELAYED_WORK(&control_usb->usb_det_wakeup_work, do_wakeup);
	}

	irq = platform_get_irq_byname(pdev, "otg_bvalid");
	if (irq > 0) {
		ret =
		    request_irq(irq, bvalid_irq_handler, 0, "otg_bvalid", NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
			return ret;
		}

		/* clear & enable bvalid irq */
		control_usb->grf_uoc0_base->CON3 = (3 << 30) | (3 << 14);

		if (control_usb->usb_irq_wakeup)
			enable_irq_wake(irq);
	}

	return ret;
}

static int usb_grf_ioremap(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	void *grf_soc_status0;
	void *grf_uoc0_base;
	void *grf_uoc1_base;
	void *grf_uoc2_base;
	void *grf_uoc3_base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_SOC_STATUS0");
	grf_soc_status0 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_soc_status0)) {
		ret = PTR_ERR(grf_soc_status0);
		return ret;
	}
	control_usb->grf_soc_status0_rk3188 =
	    (pGRF_SOC_STATUS_RK3188) grf_soc_status0;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_UOC0_BASE");
	grf_uoc0_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_uoc0_base)) {
		ret = PTR_ERR(grf_uoc0_base);
		return ret;
	}
	control_usb->grf_uoc0_base = (pGRF_UOC0_REG) grf_uoc0_base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_UOC1_BASE");
	grf_uoc1_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_uoc1_base)) {
		ret = PTR_ERR(grf_uoc1_base);
		return ret;
	}
	control_usb->grf_uoc1_base = (pGRF_UOC1_REG) grf_uoc1_base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_UOC2_BASE");
	grf_uoc2_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_uoc2_base)) {
		ret = PTR_ERR(grf_uoc2_base);
		return ret;
	}
	control_usb->grf_uoc2_base = (pGRF_UOC2_REG) grf_uoc2_base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_UOC3_BASE");
	grf_uoc3_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_uoc3_base)) {
		ret = PTR_ERR(grf_uoc3_base);
		return ret;
	}
	control_usb->grf_uoc3_base = (pGRF_UOC3_REG) grf_uoc3_base;

	return ret;
}

#ifdef CONFIG_OF

static const struct of_device_id dwc_otg_control_usb_id_table[] = {
	{
	 .compatible = "rockchip,rk3188-dwc-control-usb",
	 },
	{},
};

MODULE_DEVICE_TABLE(of, dwc_otg_control_usb_id_table);
#endif

static int dwc_otg_control_usb_probe(struct platform_device *pdev)
{
	int gpio, err;
	struct device_node *np = pdev->dev.of_node;
	struct clk *hclk_usb_peri;
	int ret = 0;

	control_usb =
	    devm_kzalloc(&pdev->dev, sizeof(*control_usb), GFP_KERNEL);

	if (!control_usb) {
		dev_err(&pdev->dev, "unable to alloc memory for control usb\n");
		ret = -ENOMEM;
		goto err1;
	}

	control_usb->chip_id = RK3188_USB_CTLR;
	control_usb->remote_wakeup = of_property_read_bool(np,
							   "rockchip,remote_wakeup");
	control_usb->usb_irq_wakeup = of_property_read_bool(np,
							    "rockchip,usb_irq_wakeup");

	hclk_usb_peri = devm_clk_get(&pdev->dev, "hclk_usb_peri");
	if (IS_ERR(hclk_usb_peri)) {
		dev_err(&pdev->dev, "Failed to get hclk_usb_peri\n");
		ret = -EINVAL;
		goto err1;
	}

	control_usb->hclk_usb_peri = hclk_usb_peri;
	clk_prepare_enable(hclk_usb_peri);

	ret = usb_grf_ioremap(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to ioremap usb grf\n");
		goto err2;
	}

	/* init host gpio */
	control_usb->host_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->host_gpios) {
		dev_err(&pdev->dev, "unable to alloc memory for host_gpios\n");
		ret = -ENOMEM;
		goto err2;
	}

	gpio = of_get_named_gpio(np, "gpios", 0);
	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "invalid host gpio%d\n", gpio);
		ret = -EINVAL;
		goto err2;
	}

	control_usb->host_gpios->gpio = gpio;

	err = devm_gpio_request(&pdev->dev, gpio, "host_drv_gpio");
	if (err) {
		dev_err(&pdev->dev,
			"failed to request GPIO%d for host_drv\n", gpio);
		ret = err;
		goto err2;
	}
	gpio_direction_output(control_usb->host_gpios->gpio, 1);

	/* init otg gpio */
	control_usb->otg_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->otg_gpios) {
		dev_err(&pdev->dev, "unable to alloc memory for otg_gpios\n");
		ret = -ENOMEM;
		goto err2;
	}

	gpio = of_get_named_gpio(np, "gpios", 1);
	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "invalid otg gpio%d\n", gpio);
		ret = -EINVAL;
		goto err2;
	}
	control_usb->otg_gpios->gpio = gpio;
	err = devm_gpio_request(&pdev->dev, gpio, "otg_drv_gpio");
	if (err) {
		dev_err(&pdev->dev,
			"failed to request GPIO%d for otg_drv\n", gpio);
		ret = err;
		goto err2;
	}
	gpio_direction_output(control_usb->otg_gpios->gpio, 0);

	ret = otg_irq_detect_init(pdev);
	if (ret < 0)
		goto err2;

	return 0;

err2:
	clk_disable_unprepare(hclk_usb_peri);
err1:
	return ret;
}

static int dwc_otg_control_usb_remove(struct platform_device *pdev)
{
	clk_disable_unprepare(control_usb->hclk_usb_peri);
	return 0;
}

static struct platform_driver dwc_otg_control_usb_driver = {
	.probe = dwc_otg_control_usb_probe,
	.remove = dwc_otg_control_usb_remove,
	.driver = {
		   .name = "rk3188-dwc-control-usb",
		   .owner = THIS_MODULE,
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
