#ifdef CONFIG_ARM64
#include "usbdev_rk.h"
#include "dwc_otg_regs.h"

static struct dwc_otg_control_usb *control_usb;

static u32 uoc_read(u32 reg)
{
	unsigned int val;

	regmap_read(control_usb->grf, reg, &val);
	return val;
}

static void uoc_write(u32 value, u32 reg)
{
	regmap_write(control_usb->grf, reg, value);
}

#ifdef CONFIG_USB20_OTG
static void usb20otg_hw_init(void)
{
	/* Turn off differential receiver in suspend mode */
	uoc_write(UOC_HIWORD_UPDATE(0, 1, 2), 0x798);

	/* Set disconnect detection trigger point to 625mv */
	uoc_write(UOC_HIWORD_UPDATE(0x9, 0xf, 11), 0x79c);

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->otg_gpios->gpio))
		gpio_set_value(control_usb->otg_gpios->gpio, 0);
}

static void usb20otg_phy_suspend(void *pdata, int suspend)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	if (suspend) {
		/* enable soft control */
		uoc_write(UOC_HIWORD_UPDATE(0x1d1, 0x1ff, 0), 0x700);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		uoc_write(UOC_HIWORD_UPDATE(0x0, 0x1, 0), 0x700);
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

	switch (rst_type) {
	case RST_POR:
		/* PHY reset */
		uoc_write(UOC_HIWORD_UPDATE(0x1, 0x3, 0), 0x700);
		reset_control_assert(rst_otg_p);
		udelay(15);
		uoc_write(UOC_HIWORD_UPDATE(0x2, 0x3, 0), 0x700);
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
	case RST_CHN_HALT:
		/* PHY reset */
		uoc_write(UOC_HIWORD_UPDATE(0x1, 0x3, 0), 0x700);
		reset_control_assert(rst_otg_p);
		udelay(15);
		uoc_write(UOC_HIWORD_UPDATE(0x2, 0x3, 0), 0x700);
		udelay(1500);
		reset_control_deassert(rst_otg_p);
		udelay(2);
		break;
	default:
		break;
	}
}

static void usb20otg_clock_init(void *pdata)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "otg");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get otg clk\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "sclk_otgphy0");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get sclk_otgphy0\n");
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
	u32 soc_status15 = uoc_read(control_usb->grf_otg_st_offset);

	switch (id) {
	case USB_STATUS_BVABLID:
		/* bvalid in grf */
		ret = soc_status15 & (0x1 << 23);
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = soc_status15 & (0x3 << 24);
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = soc_status15 & (0x1 << 26);
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
	return false;
}

static void dwc_otg_uart_mode(void *pdata, int enter_usb_uart_mode)
{
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

		rk_battery_charger_detect_cb(USB_OTG_POWER_OFF);
	} else if (1 == enable) {
		/* enable otg_drv power */
		if (gpio_is_valid(control_usb->otg_gpios->gpio))
			gpio_set_value(control_usb->otg_gpios->gpio, 1);

		if (!usb20otg_get_status(USB_STATUS_BVABLID))
			rk_battery_charger_detect_cb(USB_OTG_POWER_ON);
	}
}

struct dwc_otg_platform_data usb20otg_pdata_rk3368 = {
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

#ifdef CONFIG_USB_EHCI_RK
static void usb20ehci_hw_init(void)
{
	/* Turn off differential receiver in suspend mode */
	uoc_write(UOC_HIWORD_UPDATE(0, 1, 2), 0x7b8);
	/* Set disconnect detection trigger point to 625mv */
	uoc_write(UOC_HIWORD_UPDATE(0x9, 0xf, 11), 0x7bc);

	/* other haredware init,include:
	 * DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->host_gpios->gpio)) {
		if (!gpio_get_value(control_usb->host_gpios->gpio))
			gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

static void usb20ehci_phy_suspend(void *pdata, int suspend)
{
	struct rkehci_platform_data *usbpdata = pdata;

	if (suspend) {
		/* enable soft control */
		uoc_write(UOC_HIWORD_UPDATE(0x1d1, 0x1ff, 0), 0x728);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		uoc_write(UOC_HIWORD_UPDATE(0x0, 0x1, 0), 0x728);
		usbpdata->phy_status = 0;
	}
}

static void usb20ehci_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
	struct rkehci_platform_data *usbpdata = pdata;
	struct reset_control *rst_host_h, *rst_host_p, *rst_host_c;

	rst_host_h = devm_reset_control_get(usbpdata->dev, "host_ahb");
	rst_host_p = devm_reset_control_get(usbpdata->dev, "host_phy");
	rst_host_c = devm_reset_control_get(usbpdata->dev, "host_controller");
	if (IS_ERR(rst_host_h) || IS_ERR(rst_host_p) || IS_ERR(rst_host_c)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	switch (rst_type) {
	case RST_POR:
		/* PHY reset */
		uoc_write(UOC_HIWORD_UPDATE(0x1, 0x3, 0), 0x728);
		reset_control_assert(rst_host_p);
		udelay(15);
		uoc_write(UOC_HIWORD_UPDATE(0x2, 0x3, 0), 0x728);

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

static void usb20ehci_clock_init(void *pdata)
{
}

static void usb20ehci_clock_enable(void *pdata, int enable)
{
}

static int usb20ehci_get_status(int id)
{
	/* For HOST port in rk336x can not get any info from GRF */
	return -ENOENT;
}

struct rkehci_platform_data usb20ehci_pdata_rk3368 = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.phy_status = 0,
	.hw_init = usb20ehci_hw_init,
	.phy_suspend = usb20ehci_phy_suspend,
	.soft_reset = usb20ehci_soft_reset,
	.clock_init = usb20ehci_clock_init,
	.clock_enable = usb20ehci_clock_enable,
	.get_status = usb20ehci_get_status,
};
#endif

struct dwc_otg_platform_data usb20ohci_pdata_rk3368;

#ifdef CONFIG_OF
static const struct of_device_id rk_usb_control_id_table[] = {
	{
	 .compatible = "rockchip,rk3368-usb-control",
	 },
	{},
};
#endif
/*********************************************************************
			rk3126 usb detections
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
	uoc_write(UOC_HIWORD_UPDATE(0x1, 0x1, 3), 0x6a0);
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

	wake_lock_init(&control_usb->usb_wakelock, WAKE_LOCK_SUSPEND,
		       "usb_detect");
	INIT_DELAYED_WORK(&control_usb->usb_det_wakeup_work, do_wakeup);

	/*register otg_bvalid irq */
	irq = platform_get_irq_byname(pdev, "otg_bvalid");
	if ((irq > 0) && control_usb->usb_irq_wakeup) {
		ret = request_irq(irq, bvalid_irq_handler,
				  0, "otg_bvalid", NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
		} else {
			/* enable bvalid irq  */
			uoc_write(UOC_HIWORD_UPDATE(0x1, 0x1, 3), 0x680);
		}
	}

	return 0;
}

/********** end of usb detections **********/
#ifdef CONFIG_OF
static const struct of_device_id dwc_otg_control_usb_id_table[] = {
	{
	 .compatible = "rockchip,rk3368-dwc-control-usb",
	 },
	{},
};
#endif
static int dwc_otg_control_usb_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct clk *hclk_usb_peri;
	struct regmap *grf;
	int gpio, ret = 0;
	u32 offset;

	control_usb = devm_kzalloc(dev, sizeof(*control_usb), GFP_KERNEL);
	if (!control_usb) {
		dev_err(&pdev->dev, "Unable to alloc memory for control usb\n");
		return -ENOMEM;
	}

	/* Init regmap GRF */
	grf = syscon_regmap_lookup_by_phandle(dev->of_node, "rockchip,grf");
	if (IS_ERR(grf)) {
		dev_err(&pdev->dev, "Missing rockchip,grf property\n");
		return PTR_ERR(grf);
	}
	control_usb->grf = grf;

	/* get the reg offset of GRF_SOC_STATUS for USB2.0 OTG */
	if (of_property_read_u32(np, "grf-offset", &offset)) {
		dev_err(&pdev->dev, "missing reg property in node %s\n",
			np->name);
		return -EINVAL;
	}
	control_usb->grf_otg_st_offset = offset;

	/* Init Vbus-drv GPIOs */
	control_usb->host_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->host_gpios) {
		dev_err(&pdev->dev, "Unable to alloc memory for host_gpios\n");
		return -ENOMEM;
	}

	gpio = of_get_named_gpio(np, "host_drv_gpio", 0);
	control_usb->host_gpios->gpio = gpio;

	if (gpio_is_valid(gpio)) {
		if (devm_gpio_request(&pdev->dev, gpio, "usb_host_drv")) {
			dev_err(&pdev->dev,
				"Failed to request GPIO%d for host_drv\n",
				gpio);
			return -EINVAL;
		}
		gpio_direction_output(control_usb->host_gpios->gpio, 1);
	}

	control_usb->otg_gpios =
	    devm_kzalloc(&pdev->dev, sizeof(struct gpio), GFP_KERNEL);
	if (!control_usb->otg_gpios) {
		dev_err(&pdev->dev, "Unable to alloc memory for otg_gpios\n");
		return -ENOMEM;
	}

	gpio = of_get_named_gpio(np, "otg_drv_gpio", 0);
	control_usb->otg_gpios->gpio = gpio;

	if (gpio_is_valid(gpio)) {
		if (devm_gpio_request(&pdev->dev, gpio, "usb_otg_drv")) {
			dev_err(&pdev->dev,
				"failed to request GPIO%d for otg_drv\n", gpio);
			return -EINVAL;
		}
		gpio_direction_output(control_usb->otg_gpios->gpio, 0);
	}


	control_usb->remote_wakeup = of_property_read_bool(np,
							   "rockchip,remote_wakeup");
	control_usb->usb_irq_wakeup = of_property_read_bool(np,
							    "rockchip,usb_irq_wakeup");

	/* Init hclk_usb_peri */
	hclk_usb_peri = devm_clk_get(&pdev->dev, "hclk_usb_peri");
	if (IS_ERR(hclk_usb_peri)) {
		dev_info(&pdev->dev, "no hclk_usb_peri clk specified\n");
		hclk_usb_peri = NULL;
	}
	control_usb->hclk_usb_peri = hclk_usb_peri;
	clk_prepare_enable(hclk_usb_peri);

#ifdef CONFIG_USB20_OTG
	INIT_DELAYED_WORK(&control_usb->usb_charger_det_work,
			  usb_battery_charger_detect_work);

	if (usb20otg_get_status(USB_STATUS_BVABLID))
		schedule_delayed_work(&control_usb->usb_charger_det_work,
				      HZ / 10);
#endif

	ret = otg_irq_detect_init(pdev);
	if (ret < 0)
		goto err;

	return 0;

err:
	clk_disable_unprepare(hclk_usb_peri);
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
		   .name = "rk3368-dwc-control-usb",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(dwc_otg_control_usb_id_table),
		   },
};

static int __init dwc_otg_control_usb_init(void)
{
	int retval = 0;

	retval |= platform_driver_register(&dwc_otg_control_usb_driver);
	return retval;
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
#endif
