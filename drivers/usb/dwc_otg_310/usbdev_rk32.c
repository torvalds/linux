
#include "usbdev_rk.h"
#include "usbdev_grf_regs.h"
#include "dwc_otg_regs.h"
static struct dwc_otg_control_usb *control_usb;

#ifdef CONFIG_USB20_OTG
static void usb20otg_hw_init(void)
{
#ifndef CONFIG_USB20_HOST
	/* enable soft control */
	control_usb->grf_uoc2_base->CON2 = (0x01 << 2) | ((0x01 << 2) << 16);
	/* enter suspend */
	control_usb->grf_uoc2_base->CON3 = 0x2A | (0x3F << 16);
#endif
	/* usb phy config init
	 * usb phy enter usb mode */
	control_usb->grf_uoc0_base->CON3 = (0x00c0 << 16);

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
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct reset_control *rst_otg_h, *rst_otg_p, *rst_otg_c;

	rst_otg_h = devm_reset_control_get(usbpdata->dev, "otg_ahb");
	rst_otg_p = devm_reset_control_get(usbpdata->dev, "otg_phy");
	rst_otg_c = devm_reset_control_get(usbpdata->dev, "otg_controller");
	if (IS_ERR(rst_otg_h) || IS_ERR(rst_otg_p) || IS_ERR(rst_otg_c)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	reset_control_assert(rst_otg_h);
	reset_control_assert(rst_otg_p);
	reset_control_assert(rst_otg_c);
	udelay(5);
	reset_control_deassert(rst_otg_h);
	reset_control_deassert(rst_otg_p);
	reset_control_deassert(rst_otg_c);
	mdelay(2);
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
		ret = control_usb->grf_soc_status2_rk3288->otg_bvalid;
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = control_usb->grf_soc_status2_rk3288->otg_linestate;
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = control_usb->grf_soc_status2_rk3288->otg_iddig;
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
		/* bypass dm, enter uart mode */
		control_usb->grf_uoc0_base->CON3 = (0x00c0 | (0x00c0 << 16));

	} else if (0 == enter_usb_uart_mode) {
		/* enter usb mode */
		control_usb->grf_uoc0_base->CON3 = (0x00c0 << 16);
	}
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

struct dwc_otg_platform_data usb20otg_pdata_rk3288 = {
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
	/* usb phy config init
	 * set common_on = 0, in suspend mode, host1 PLL blocks remain powered.
	 * for RK3288, hsic and other modules use host1 (DWC_OTG) 480M phy clk.
	 */
	control_usb->grf_uoc2_base->CON0 = (1 << 16) | 0;

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
		control_usb->grf_uoc2_base->CON2 =
		    (0x01 << 2) | ((0x01 << 2) << 16);
		/* enter suspend */
		control_usb->grf_uoc2_base->CON3 = 0x2A | (0x3F << 16);
		usbpdata->phy_status = 1;
	} else {
		/* exit suspend */
		control_usb->grf_uoc2_base->CON2 = ((0x01 << 2) << 16);
		usbpdata->phy_status = 0;
	}
}

static void usb20host_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct reset_control *rst_host1_h, *rst_host1_p, *rst_host1_c;

	rst_host1_h = devm_reset_control_get(usbpdata->dev, "host1_ahb");
	rst_host1_p = devm_reset_control_get(usbpdata->dev, "host1_phy");
	rst_host1_c = devm_reset_control_get(usbpdata->dev, "host1_controller");
	if (IS_ERR(rst_host1_h) || IS_ERR(rst_host1_p) || IS_ERR(rst_host1_c)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	reset_control_assert(rst_host1_h);
	reset_control_assert(rst_host1_p);
	reset_control_assert(rst_host1_c);
	udelay(5);
	reset_control_deassert(rst_host1_h);
	reset_control_deassert(rst_host1_p);
	reset_control_deassert(rst_host1_c);
	mdelay(2);
}

static void usb20host_clock_init(void *pdata)
{
	struct dwc_otg_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk, *phyclk_480m;

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

	phyclk_480m = devm_clk_get(usbpdata->dev, "usbphy_480m");
	if (IS_ERR(phyclk_480m)) {
		dev_err(usbpdata->dev, "Failed to get usbphy_480m\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
	usbpdata->phyclk_480m = phyclk_480m;
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
		ret = control_usb->grf_soc_status2_rk3288->host1_bvalid;
		break;
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = control_usb->grf_soc_status2_rk3288->host1_linestate;
		break;
	case USB_STATUS_ID:
		/* id in grf */
		ret = control_usb->grf_soc_status2_rk3288->host1_iddig;
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

struct dwc_otg_platform_data usb20host_pdata_rk3288 = {
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
	control_usb->grf_uoc3_base->CON0 = ((0xf << 6) << 16) | (0xf << 6);

	/* other haredware init
	 * set common_on = 0, in suspend mode,
	 * otg/host PLL blocks remain powered
	 * for RK3288, use host1 (DWC_OTG) 480M phy clk
	 */
	control_usb->grf_uoc2_base->CON0 = (1 << 16) | 0;

	/* change INCR to INCR16 or INCR8(beats less than 16)
	 * or INCR4(beats less than 8) or SINGLE(beats less than 4)
	 */
	control_usb->grf_uoc4_base->CON0 = 0x00ff00bc;
}

static void rk_hsic_clock_init(void *pdata)
{
	/* By default, hsicphy_480m's parent is otg phy 480MHz clk
	 * rk3188 must use host phy 480MHz clk, because if otg bypass
	 * to uart mode, otg phy 480MHz clk will be closed automatically
	 */
	struct rkehci_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk480m_hsic, *phyclk12m_hsic;

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

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_hsic");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_hsic\n");
		return;
	}

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
	struct rkehci_platform_data *usbpdata = pdata;
	struct reset_control *rst_hsic_h, rst_hsic_a, rst_hsic_p;

	rst_hsic_h = devm_reset_control_get(usbpdata->dev, "hsic_ahb");
	rst_hsic_a = devm_reset_control_get(usbpdata->dev, "hsic_aux");
	rst_hsic_p = devm_reset_control_get(usbpdata->dev, "hsic_phy");

	reset_control_assert(rst_hsic_h);
	reset_control_assert(rst_hsic_a);
	reset_control_assert(rst_hsic_p);
	udelay(5);
	reset_control_deassert(rst_hsic_h);
	reset_control_deassert(rst_hsic_a);
	reset_control_deassert(rst_hsic_p);
	mdelay(2);

	/* HSIC per-port reset */
	control_usb->grf_uoc3_base->CON0 = ((1 << 10) << 16) | (1 << 10);
	udelay(2);
	control_usb->grf_uoc3_base->CON0 = ((1 << 10) << 16) | (0 << 10);
	udelay(2);
}

struct rkehci_platform_data rkhsic_pdata_rk3288 = {
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

#ifdef CONFIG_USB_EHCI_RK
static void rk_ehci_hw_init(void)
{
	/* usb phy config init */

	/* DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->host_gpios->gpio)) {
		if (!gpio_get_value(control_usb->host_gpios->gpio))
			gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

static void rk_ehci_phy_suspend(void *pdata, int suspend)
{
	struct rkehci_platform_data *usbpdata = pdata;

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

static void rk_ehci_clock_init(void *pdata)
{
	struct rkehci_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_usb2");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_usb2\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "clk_usbphy2");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get clk_usbphy2\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}

static void rk_ehci_clock_enable(void *pdata, int enable)
{
	struct rkehci_platform_data *usbpdata = pdata;

	if (enable == usbpdata->clk_status)
		return;
	if (enable) {
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
		usbpdata->clk_status = 1;
	} else {
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
		usbpdata->clk_status = 0;
	}
}

static void rk_ehci_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
	struct rkehci_platform_data *usbpdata = pdata;
	struct reset_control *rst_host0_h, *rst_host0_p,
			     *rst_host0_c , *rst_host0;

	rst_host0_h = devm_reset_control_get(usbpdata->dev, "ehci_ahb");
	rst_host0_p = devm_reset_control_get(usbpdata->dev, "ehci_phy");
	rst_host0_c = devm_reset_control_get(usbpdata->dev, "ehci_controller");
	rst_host0 = devm_reset_control_get(usbpdata->dev, "ehci");
	if (IS_ERR(rst_host0_h) || IS_ERR(rst_host0_p) ||
	    IS_ERR(rst_host0_c) || IS_ERR(rst_host0)) {
		dev_err(usbpdata->dev, "Fail to get reset control from dts\n");
		return;
	}

	reset_control_assert(rst_host0_h);
	reset_control_assert(rst_host0_p);
	reset_control_assert(rst_host0_c);
	reset_control_assert(rst_host0);
	udelay(5);
	reset_control_deassert(rst_host0_h);
	reset_control_deassert(rst_host0_p);
	reset_control_deassert(rst_host0_c);
	reset_control_deassert(rst_host0);
	mdelay(2);
}

static int rk_ehci_get_status(int id)
{
	int ret = -1;

	switch (id) {
	case USB_STATUS_DPDM:
		/* dpdm in grf */
		ret = control_usb->grf_soc_status2_rk3288->host0_linestate;
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

struct rkehci_platform_data rkehci_pdata_rk3288 = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.clk_status = -1,
	.phy_status = 0,
	.hw_init = rk_ehci_hw_init,
	.phy_suspend = rk_ehci_phy_suspend,
	.clock_init = rk_ehci_clock_init,
	.clock_enable = rk_ehci_clock_enable,
	.soft_reset = rk_ehci_soft_reset,
	.get_status = rk_ehci_get_status,
};
#endif

#ifdef CONFIG_USB_OHCI_HCD_RK
static void rk_ohci_hw_init(void)
{
	/* usb phy config init */

	/* DRV_VBUS GPIO init */
	if (gpio_is_valid(control_usb->host_gpios->gpio)) {
		if (!gpio_get_value(control_usb->host_gpios->gpio))
			gpio_set_value(control_usb->host_gpios->gpio, 1);
	}
}

static void rk_ohci_clock_init(void *pdata)
{
	struct rkehci_platform_data *usbpdata = pdata;
	struct clk *ahbclk, *phyclk;

	ahbclk = devm_clk_get(usbpdata->dev, "hclk_usb3");
	if (IS_ERR(ahbclk)) {
		dev_err(usbpdata->dev, "Failed to get hclk_usb3\n");
		return;
	}

	phyclk = devm_clk_get(usbpdata->dev, "clk_usbphy3");
	if (IS_ERR(phyclk)) {
		dev_err(usbpdata->dev, "Failed to get clk_usbphy3\n");
		return;
	}

	usbpdata->phyclk = phyclk;
	usbpdata->ahbclk = ahbclk;
}

static void rk_ohci_clock_enable(void *pdata, int enable)
{
	struct rkehci_platform_data *usbpdata = pdata;

	if (enable == usbpdata->clk_status)
		return;
	if (enable) {
		clk_prepare_enable(usbpdata->ahbclk);
		clk_prepare_enable(usbpdata->phyclk);
		usbpdata->clk_status = 1;
	} else {
		clk_disable_unprepare(usbpdata->ahbclk);
		clk_disable_unprepare(usbpdata->phyclk);
		usbpdata->clk_status = 0;
	}
}

static void rk_ohci_soft_reset(void *pdata, enum rkusb_rst_flag rst_type)
{
}

struct rkehci_platform_data rkohci_pdata_rk3288 = {
	.phyclk = NULL,
	.ahbclk = NULL,
	.clk_status = -1,
	.hw_init = rk_ohci_hw_init,
	.clock_init = rk_ohci_clock_init,
	.clock_enable = rk_ohci_clock_enable,
	.soft_reset = rk_ohci_soft_reset,
};
#endif

/*********************************************************************
			rk3288 usb detections
*********************************************************************/

#define WAKE_LOCK_TIMEOUT (HZ * 10)
static inline void do_wakeup(struct work_struct *work)
{
	/* wake up the system */
	rk_send_wakeup_key();
}

static void usb_battery_charger_detect_work(struct work_struct *work)
{
	rk_usb_charger_status = usb_battery_charger_detect(0);
}

/********** handler for bvalid irq **********/
static irqreturn_t bvalid_irq_handler(int irq, void *dev_id)
{
	/* clear irq */
	control_usb->grf_uoc0_base->CON4 = (0x0008 | (0x0008 << 16));

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

	rk_usb_charger_status = USB_BC_TYPE_SDP;
	schedule_delayed_work(&control_usb->usb_charger_det_work, HZ / 10);

	return IRQ_HANDLED;
}

/***** handler for otg id rise and fall edge *****/
static irqreturn_t id_irq_handler(int irq, void *dev_id)
{
	unsigned int uoc_con;

	/* clear irq */
	uoc_con = control_usb->grf_uoc0_base->CON4;

	/* id rise */
	if (uoc_con & (1 << 5)) {
		/* clear id rise irq pandding */
		control_usb->grf_uoc0_base->CON4 = ((1 << 5) | (1 << 21));
	}

	/* id fall */
	if (uoc_con & (1 << 7)) {
#ifdef CONFIG_RK_USB_UART
		/* usb otg dp/dm switch to usb phy */
		dwc_otg_uart_mode(NULL, PHY_USB_MODE);
#endif
		/* clear id fall irq pandding */
		control_usb->grf_uoc0_base->CON4 = ((1 << 7) | (1 << 23));
	}

	if (control_usb->usb_irq_wakeup) {
		wake_lock_timeout(&control_usb->usb_wakelock,
				  WAKE_LOCK_TIMEOUT);
		schedule_delayed_work(&control_usb->usb_det_wakeup_work,
				      HZ / 10);
	}

	return IRQ_HANDLED;
}

#ifdef USB_LINESTATE_IRQ
/***** handler for usb line status change *****/

static irqreturn_t line_irq_handler(int irq, void *dev_id)
{
	/* clear irq */

	if (control_usb->grf_uoc0_base->CON0 & 1 << 15)
		control_usb->grf_uoc0_base->CON0 = (1 << 15 | 1 << 31);

	if (control_usb->grf_uoc1_base->CON0 & 1 << 15)
		control_usb->grf_uoc1_base->CON0 = (1 << 15 | 1 << 31);

	if (control_usb->grf_uoc2_base->CON0 & 1 << 15)
		control_usb->grf_uoc2_base->CON0 = (1 << 15 | 1 << 31);

	if (control_usb->usb_irq_wakeup) {
		wake_lock_timeout(&control_usb->usb_wakelock,
				  WAKE_LOCK_TIMEOUT);
		schedule_delayed_work(&control_usb->usb_det_wakeup_work,
				      HZ / 10);
	}

	return IRQ_HANDLED;
}
#endif

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
			control_usb->grf_uoc0_base->CON4 = 0x000c000c;
		}
	}

	/*register otg_id irq */
	irq = platform_get_irq_byname(pdev, "otg_id");
	if ((irq > 0) && control_usb->usb_irq_wakeup) {
		ret = request_irq(irq, id_irq_handler, 0, "otg_id", NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
		} else {
			/* enable otg_id irq */
			control_usb->grf_uoc0_base->CON4 = 0x00f000f0;
		}
	}
#if 0
	/*register otg_linestate irq */
	irq = platform_get_irq_byname(pdev, "otg_linestate");
	if (irq > 0) {
		ret =
		    request_irq(irq, line_irq_handler, 0, "otg_linestate",
				NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
			return ret;
		} else {
			control_usb->grf_uoc0_base->CON0 = 0xc000c000;
			if (control_usb->usb_irq_wakeup)
				enable_irq_wake(irq);
		}
	}

	/*register host0_linestate irq */
	irq = platform_get_irq_byname(pdev, "host0_linestate");
	if (irq > 0) {
		ret =
		    request_irq(irq, line_irq_handler, 0, "host0_linestate",
				NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
			return ret;
		} else {
			control_usb->grf_uoc1_base->CON0 = 0xc000c000;
			if (control_usb->usb_irq_wakeup)
				enable_irq_wake(irq);
		}
	}

	/*register host1_linestate irq */
	irq = platform_get_irq_byname(pdev, "host1_linestate");
	if (irq > 0) {
		ret =
		    request_irq(irq, line_irq_handler, 0, "host1_linestate",
				NULL);
		if (ret < 0) {
			dev_err(&pdev->dev, "request_irq %d failed!\n", irq);
			return ret;
		} else {
			control_usb->grf_uoc2_base->CON0 = 0xc000c000;
			if (control_usb->usb_irq_wakeup)
				enable_irq_wake(irq);
		}
	}
#endif
	return ret;
}

/********** end of rk3288 usb detections **********/

static int usb_grf_ioremap(struct platform_device *pdev)
{
	int ret = 0;
	struct resource *res;
	void *grf_soc_status1;
	void *grf_soc_status2;
	void *grf_soc_status19;
	void *grf_soc_status21;
	void *grf_uoc0_base;
	void *grf_uoc1_base;
	void *grf_uoc2_base;
	void *grf_uoc3_base;
	void *grf_uoc4_base;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_SOC_STATUS1");
	grf_soc_status1 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_soc_status1)) {
		ret = PTR_ERR(grf_soc_status1);
		return ret;
	}
	control_usb->grf_soc_status1_rk3288 =
	    (pGRF_SOC_STATUS1_RK3288) grf_soc_status1;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_SOC_STATUS2");
	grf_soc_status2 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_soc_status2)) {
		ret = PTR_ERR(grf_soc_status2);
		return ret;
	}
	control_usb->grf_soc_status2_rk3288 =
	    (pGRF_SOC_STATUS2_RK3288) grf_soc_status2;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_SOC_STATUS19");
	grf_soc_status19 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_soc_status19)) {
		ret = PTR_ERR(grf_soc_status19);
		return ret;
	}
	control_usb->grf_soc_status19_rk3288 =
	    (pGRF_SOC_STATUS19_RK3288) grf_soc_status19;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_SOC_STATUS21");
	grf_soc_status21 = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_soc_status21)) {
		ret = PTR_ERR(grf_soc_status21);
		return ret;
	}
	control_usb->grf_soc_status21_rk3288 =
	    (pGRF_SOC_STATUS21_RK3288) grf_soc_status21;

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

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
					   "GRF_UOC4_BASE");
	grf_uoc4_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(grf_uoc4_base)) {
		ret = PTR_ERR(grf_uoc4_base);
		return ret;
	}
	control_usb->grf_uoc4_base = (pGRF_UOC4_REG) grf_uoc4_base;

	return ret;
}

#ifdef CONFIG_OF

static const struct of_device_id rk_usb_control_id_table[] = {
	{
	 .compatible = "rockchip,rk3288-usb-control",
	 },
	{},
};

#endif

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

	control_usb->chip_id = RK3288_USB_CTLR;
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
		   .name = "rk3288-usb-control",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(rk_usb_control_id_table),
		   },
};

#ifdef CONFIG_OF

static const struct of_device_id dwc_otg_control_usb_id_table[] = {
	{
	 .compatible = "rockchip,rk3288-dwc-control-usb",
	 },
	{},
};

#endif

static int dwc_otg_control_usb_probe(struct platform_device *pdev)
{
	struct clk *hclk_usb_peri, *phyclk_480m, *phyclk480m_parent;
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

	phyclk480m_parent = devm_clk_get(&pdev->dev, "usbphy2_480m");
	if (IS_ERR(phyclk480m_parent)) {
		dev_err(&pdev->dev, "Failed to get usbphy2_480m\n");
		goto err2;
	}

	phyclk_480m = devm_clk_get(&pdev->dev, "usbphy_480m");
	if (IS_ERR(phyclk_480m)) {
		dev_err(&pdev->dev, "Failed to get usbphy_480m\n");
		goto err2;
	}

	clk_set_parent(phyclk_480m, phyclk480m_parent);

	ret = usb_grf_ioremap(pdev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to ioremap usb grf\n");
		goto err2;
	}
#ifdef CONFIG_USB20_OTG
	if (usb20otg_get_status(USB_STATUS_BVABLID)) {
		rk_usb_charger_status = USB_BC_TYPE_SDP;
		schedule_delayed_work(&control_usb->usb_charger_det_work,
				      HZ / 10);
	}
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
		   .name = "rk3288-dwc-control-usb",
		   .owner = THIS_MODULE,
		   .of_match_table = of_match_ptr(dwc_otg_control_usb_id_table),
		   },
};

static int __init dwc_otg_control_usb_init(void)
{
	int retval = 0;

	retval = platform_driver_register(&rk_usb_control_driver);
	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}

	retval = platform_driver_register(&dwc_otg_control_usb_driver);

	if (retval < 0) {
		printk(KERN_ERR "%s retval=%d\n", __func__, retval);
		return retval;
	}
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
