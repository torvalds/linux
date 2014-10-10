#include "usbdev_rk.h"
#include "usbdev_grf_regs.h"
#include "dwc_otg_regs.h"
static struct dwc_otg_control_usb *control_usb;

#ifdef CONFIG_USB20_OTG
static void usb20otg_hw_init(void)
{
	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->otg_gpios->gpio)) {
		if (gpio_get_value(control_usb->otg_gpios->gpio))
			gpio_set_value(control_usb->otg_gpios->gpio, 0);
	}
}

static void usb20otg_phy_suspend(void *pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (suspend) {
		/* enable soft control */
		writel(UOC_HIWORD_UPDATE(0x55, 0x7f, 0),
		       RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		writel(UOC_HIWORD_UPDATE(0x0, 0x1, 0),
		       RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
		usbpdata->phy_status = 0;
	}
}

static void usb20otg_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct reset_control *rst_otg_h, *rst_otg_p, *rst_otg_c;

	rst_otg_h = devm_reset_control_get(usbpdata->dev, "otg_ahb");
	rst_otg_p = devm_reset_control_get(usbpdata->dev, "otg_phy");
	rst_otg_c = devm_reset_control_get(usbpdata->dev, "otg_controller");
	if (IS_ERR(rst_otg_h) || IS_ERR(rst_otg_p) || IS_ERR(rst_otg_c)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	switch(rst_type) {
	case RST_POR:
		/* PHY reset */
		writel(UOC_HIWORD_UPDATE(0x1, 0x3, 0),
			   RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
		reset_control_assert(rst_otg_p);
		udelay(15);
		writel(UOC_HIWORD_UPDATE(0x2, 0x3, 0),
			   RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
		udelay(1500);
		reset_control_deassert(rst_otg_p);
		udelay(2);

		/* Controller reset */
		reset_control_assert(rst_otg_c);
		reset_control_assert(rst_otg_h);

		udelay(2);

		reset_control_deassert(rst_otg_c);
		reset_control_deassert(rst_otg_h);
		break;

	default:
		break;
	}
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
		/*
		   clk_disable_unprepare(usbpdata->phyclk);
		 */
	}
}

static int usb20otg_get_status(int id)
{
	int ret = -1;
	u32 soc_status0 = readl(RK_GRF_VIRT + RK3036_GRF_SOC_STATUS0);

	switch (id) {
	case USB_STATUS_BVABLID:
		/* bvalid in grf */
		ret = soc_status0 & (0x1 << 8);
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = soc_status0 & (0x3 << 9);
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = soc_status0 & (0x1 << 11);
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
/**
 *  dwc_otg_uart_enabled - check if a usb-uart bypass func is enabled in DT
 *
 *  Returns true if the status property of node "usb_uart" is set to "okay"
 *  or "ok", if this property is absent it will use the default status "ok"
 *  0 otherwise
 */
static bool dwc_otg_uart_enabled(void)
{
	struct device_node *np;

	np = of_find_node_by_name(NULL, "usb_uart");
	if (np && of_device_is_available(np))
		return true;

	return false;
}

static void dwc_otg_uart_mode(void *pdata, int enter_usb_uart_mode)
{
	if ((1 == enter_usb_uart_mode) && dwc_otg_uart_enabled()) {
		/* bypass dm, enter uart mode */
		writel(UOC_HIWORD_UPDATE(0x3, 0x3, 12), RK_GRF_VIRT + 
			   RK3036_GRF_UOC1_CON4);
	} else if (0 == enter_usb_uart_mode) {
		/* enter usb mode */
		writel(UOC_HIWORD_UPDATE(0x0, 0x3, 12), RK_GRF_VIRT + 
			   RK3036_GRF_UOC1_CON4);
	}
}
#else
static void dwc_otg_uart_mode(void *pdata, int enter_usb_uart_mode)
{
}
#endif

static void usb20otg_power_enable(int enable)
{
	if (0 == enable) {
		/* disable otg_drv power */
		if (gpio_is_valid(control_usb->otg_gpios->gpio))
			gpio_set_value(control_usb->otg_gpios->gpio, 0);
	} else if (1 == enable) {
		/* enable otg_drv power */
		if (gpio_is_valid(control_usb->otg_gpios->gpio))
			gpio_set_value(control_usb->otg_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20otg_pdata_rk3036 = {
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
	.dwc_otg_uart_mode = dwc_otg_uart_mode,
	.bc_detect_cb = rk_battery_charger_detect_cb,
};
#endif

#ifdef CONFIG_USB20_HOST
static void usb20host_hw_init(void)
{
	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->host_gpios->gpio)) {
		if (!gpio_get_value(control_usb->host_gpios->gpio))
			gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

static void usb20host_phy_suspend(void *pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata = pdata;

	if (suspend) {
		/* enable soft control */
		writel(UOC_HIWORD_UPDATE(0x1d5, 0x1ff, 0),
		       RK_GRF_VIRT + RK3036_GRF_UOC1_CON5);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		writel(UOC_HIWORD_UPDATE(0x0, 0x1, 0),
		       RK_GRF_VIRT + RK3036_GRF_UOC1_CON5);
		usbpdata->phy_status = 0;
	}
}

static void usb20host_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct reset_control *rst_host_h, *rst_host_p, *rst_host_c;

	rst_host_h = devm_reset_control_get(usbpdata->dev, "host_ahb");
	rst_host_p = devm_reset_control_get(usbpdata->dev, "host_phy");
	rst_host_c = devm_reset_control_get(usbpdata->dev, "host_controller");
	if (IS_ERR(rst_host_h) || IS_ERR(rst_host_p) || IS_ERR(rst_host_c)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	switch(rst_type) {
	case RST_POR:
		/* PHY reset */
		writel(UOC_HIWORD_UPDATE(0x1, 0x3, 0),
			   RK_GRF_VIRT + RK3036_GRF_UOC1_CON5);
		reset_control_assert(rst_host_p);
		udelay(15);
		writel(UOC_HIWORD_UPDATE(0x2, 0x3, 0),
			   RK_GRF_VIRT + RK3036_GRF_UOC1_CON5);

		udelay(1500);
		reset_control_deassert(rst_host_p);

		/* Controller reset */
		reset_control_assert(rst_host_c);
		reset_control_assert(rst_host_h);

		udelay(5);

		reset_control_deassert(rst_host_c);
		reset_control_deassert(rst_host_h);
		break;

	default:
		break;
	}
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
	u32 soc_status0 = readl(RK_GRF_VIRT + RK3036_GRF_SOC_STATUS0);

	switch (id) {
	case USB_STATUS_BVABLID:
		/* bvalid in grf */
		ret = soc_status0 & (0x1 << 13);
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = soc_status0 & (0x3 << 14);
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = 0;
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
		if (gpio_is_valid(control_usb->host_gpios->gpio))
			gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

struct dwc_otg_platform_data usb20host_pdata_rk3036 = {
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

#ifdef CONFIG_OF
static const struct of_device_id rk_usb_control_id_table[] = {
	{
	 .compatible = "rockchip,rk3036-usb-control",
	 },
	{},
};
#endif
/*********************************************************************
			rk3036 usb detections
*********************************************************************/

#define WAKE_LOCK_TIMEOUT (HZ * 10)
static inline void do_wakeup(struct work_struct *work)
{
	/* wake up the system */
	rk_send_wakeup_key();
}

static void usb_battery_charger_detect_work(struct work_struct *work)
{
	rk_battery_charger_detect_cb(usb_battery_charger_detect(1));
}

/********** handler for bvalid irq **********/
static irqreturn_t bvalid_irq_handler(int irq, void *dev_id)
{
	/* clear irq */
	writel(UOC_HIWORD_UPDATE(0x1, 0x1, 15),
	       RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
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

	schedule_delayed_work(&control_usb->usb_charger_det_work, HZ / 10);

	return IRQ_HANDLED;
}

/************* register usb detection irqs **************/
static int otg_irq_detect_init(struct platform_device *pdev)
{
	int ret = 0;
	int irq = 0;

	if (control_usb->usb_irq_wakeup) {
		wake_lock_init(&control_usb->usb_wakelock, WAKE_LOCK_SUSPEND,
			       "usb_detect");
		INIT_DELAYED_WORK(&control_usb->usb_det_wakeup_work, do_wakeup);
	}

	/*register otg_bvalid irq */
	irq = platform_get_irq_byname(pdev, "otg_bvalid");
	if ((irq > 0) && control_usb->usb_irq_wakeup) {
		ret = request_irq(irq, bvalid_irq_handler,
				  0, "otg_bvalid", NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
		} else {
			/* enable bvalid irq  */
			writel(UOC_HIWORD_UPDATE(0x1, 0x1, 14),
			       RK_GRF_VIRT + RK3036_GRF_UOC0_CON5);
		}
	}
	return ret;
}

/********** end of rk3036 usb detections **********/
static int rk_usb_control_probe(struct platform_device *pdev)
{
	int gpio, err;
	struct device_node *np = pdev->dev.of_node;
	int ret = 0;

	control_usb =
	    devm_kzalloc(&pdev->dev, sizeof(*control_usb), GFP_KERNEL);
	if (!control_usb) {
		dev_err(&pdev->dev, "unable to alloc memory for control usb\n");
		ret = -ENOMEM;
		goto out;
	}

	control_usb->chip_id = RK3036_USB_CTLR;
	control_usb->remote_wakeup = of_property_read_bool(np,
							   "rockchip,remote_wakeup");
	control_usb->usb_irq_wakeup = of_property_read_bool(np,
							    "rockchip,usb_irq_wakeup");

	INIT_DELAYED_WORK(&control_usb->usb_charger_det_work,
			  usb_battery_charger_detect_work);

	control_usb->host_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->host_gpios) {
		dev_err(&pdev->dev, "unable to alloc memory for host_gpios\n");
		ret = -ENOMEM;
		goto out;
	}

	gpio = of_get_named_gpio(np, "host_drv_gpio", 0);
	control_usb->host_gpios->gpio = gpio;

	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "invalid host gpio%d\n", gpio);
	} else {
		err = devm_gpio_request(&pdev->dev, gpio, "host_drv_gpio");
		if (err) {
			dev_err(&pdev->dev,
				"failed to request GPIO%d for host_drv\n",
				gpio);
			ret = err;
			goto out;
		}
		gpio_direction_output(control_usb->host_gpios->gpio, 1);
	}

	control_usb->otg_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->otg_gpios) {
		dev_err(&pdev->dev, "unable to alloc memory for otg_gpios\n");
		ret = -ENOMEM;
		goto out;
	}

	gpio = of_get_named_gpio(np, "otg_drv_gpio", 0);
	control_usb->otg_gpios->gpio = gpio;

	if (!gpio_is_valid(gpio)) {
		dev_err(&pdev->dev, "invalid otg gpio%d\n", gpio);
	} else {
		err = devm_gpio_request(&pdev->dev, gpio, "otg_drv_gpio");
		if (err) {
			dev_err(&pdev->dev,
				"failed to request GPIO%d for otg_drv\n", gpio);
			ret = err;
			goto out;
		}
		gpio_direction_output(control_usb->otg_gpios->gpio, 0);
	}

out:
	return ret;
}

static int rk_usb_control_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver rk_usb_control_driver = {
	.probe = rk_usb_control_probe,
	.remove = rk_usb_control_remove,
	.driver = {
		   .name = "rk3036-usb-control",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk_usb_control_id_table),
		   },
};

#ifdef CONFIG_OF

static const struct of_device_id dwc_otg_control_usb_id_table[] = {
	{
	 .compatible = "rockchip,rk3036-dwc-control-usb",
	 },
	{},
};

#endif
static int dwc_otg_control_usb_probe(struct platform_device *pdev)
{
	struct clk *hclk_usb_peri;
	int ret = 0;

	if (!control_usb) {
		dev_err(&pdev->dev, "unable to alloc memory for control usb\n");
		ret = -ENOMEM;
		goto err1;
	}

	hclk_usb_peri = devm_clk_get(&pdev->dev, "hclk_usb_peri");
	if (IS_ERR(hclk_usb_peri)) {
		dev_err(&pdev->dev, "Failed to get hclk_usb_peri\n");
		ret = -EINVAL;
		goto err1;
	}

	control_usb->hclk_usb_peri = hclk_usb_peri;
	clk_prepare_enable(hclk_usb_peri);

#ifdef CONFIG_USB20_OTG
	if (usb20otg_get_status(USB_STATUS_BVABLID))
		schedule_delayed_work(&control_usb->usb_charger_det_work,
				      HZ / 10);
#endif

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
		   .name = "rk3036-dwc-control-usb",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(dwc_otg_control_usb_id_table),
		   },
};

static int __init dwc_otg_control_usb_init(void)
{
	int retval = 0;

	retval |= platform_driver_register(&rk_usb_control_driver);
	retval |= platform_driver_register(&dwc_otg_control_usb_driver);
	return retval;
}

subsys_initcall(dwc_otg_control_usb_init);

static void __exit dwc_otg_control_usb_exit(void)
{
	platform_driver_unregister(&rk_usb_control_driver);
	platform_driver_unregister(&dwc_otg_control_usb_driver);
}

module_exit(dwc_otg_control_usb_exit);
MODULE_ALIAS("platform: dwc_control_usb");
MODULE_AUTHOR("RockChip Inc.");
MODULE_DESCRIPTION("RockChip Control Module USB Driver");
MODULE_LICENSE("GPL v2");
