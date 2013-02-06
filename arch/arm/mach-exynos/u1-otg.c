
#include <mach/regs-usb-phy.h>
#include "../../../drivers/usb/gadget/s3c_udc.h"
#include <plat/s5p-otghost.h>
#include <plat/usb-phy.h>

#define PHY_ENABLE	(1 << 0)
#define PHY_DISABLE	(0)

static int usb_status;
static u64 s3c_device_usb_otghcd_dmamask = 0xffffffffUL;

#ifdef USE_S3C_OTG_PHY
static int c210_otg_host_phy_init(int mode)
{
	struct clk *otg_clk;
	u32 value;
	int err;

	otg_clk = clk_get(NULL, "usbotg");
	if (IS_ERR(otg_clk)) {
		pr_err("otg: Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	err = clk_enable(otg_clk);
	if (err) {
		pr_err("otg: Failed to enable otg clock\n");
		clk_put(otg_clk);
		return err;
	}

	writel(PHY_ENABLE, S5P_USBOTG_PHY_CONTROL);

	value = readl(EXYNOS4_PHYCLK) & (~(1<<4) | (7<<0));
	pr_info("otg : phy clk 0x%x\n", value);
	writel(value, EXYNOS4_PHYCLK);

	value = readl(EXYNOS4_PHYPWR) & (~(7<<3) & ~(1<<0));
	pr_info("otg : phy pwr 0x%x\n", value);
	writel(value, EXYNOS4_PHYPWR);

	value = readl(EXYNOS4_RSTCON) & (~(3<<1) | (1<<0));
	writel(value, EXYNOS4_RSTCON);
	udelay(10);
	value &= ~(7<<0);
	writel(value, EXYNOS4_RSTCON);

	clk_put(otg_clk);

	return 0;
}

static int c210_otg_host_phy_exit(int mode)
{
	struct clk *otg_clk;

	otg_clk = clk_get(NULL, "usbotg");
	if (IS_ERR(otg_clk)) {
		pr_err("otg: Failed to get otg clock\n");
		return PTR_ERR(otg_clk);
	}

	writel((readl(EXYNOS4_PHYPWR) | PHY0_NORMAL_MASK),
			EXYNOS4_PHYPWR);

	writel(PHY_DISABLE, S5P_USBOTG_PHY_CONTROL);

	clk_disable(otg_clk);
	clk_put(otg_clk);

	return 0;
}
#else
static int c210_otg_host_phy_init(int mode)
{
	s5p_usb_phy_init(&s3c_device_usbgadget, S5P_USB_PHY_OTGHOST);
	return 0;
}
static int c210_otg_host_phy_exit(int mode)
{
	s5p_usb_phy_exit(&s3c_device_usbgadget, S5P_USB_PHY_OTGHOST);
	return 0;
}
#endif

static void c210_host_notify_cb(int mode)
{
	pr_info("otg host_notify : %d\n", mode);
	host_state_notify(&host_notifier_pdata.ndev, mode);
}

static struct sec_otghost_data otghost_data = {
	.clk_usage = 0,
	.set_pwr_cb = usb_otg_accessory_power,
	.sec_whlist_table_num = 1,
	.start = 0,
	.stop = 0,

	.phy_init = c210_otg_host_phy_init,
	.phy_exit = c210_otg_host_phy_exit,
	.host_notify_cb = c210_host_notify_cb,
};

static struct resource s3c_usb_otghcd_resource[] = {
	[0] = {
		.start = S5P_PA_HSOTG,
		.end   = S5P_PA_HSOTG + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_USB_HSOTG,
		.end   = IRQ_USB_HSOTG,
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device s3c_device_usb_otghcd = {
	.name		=	"s3c_otghcd",
	.id		=	-1,
	.num_resources	=	ARRAY_SIZE(s3c_usb_otghcd_resource),
	.resource	=	s3c_usb_otghcd_resource,
	.dev = {
		.platform_data	=	&otghost_data,
		.dma_mask	=	&s3c_device_usb_otghcd_dmamask,
		.coherent_dma_mask	=	0xffffffffUL,
	}
};

static char *get_usb_cable_string(int mode)
{
	switch (mode) {
	case USB_CABLE_DETACHED: return "USB Cable Detached";
	case USB_CABLE_ATTACHED: return "USB Cable Attached";
	case USB_OTGHOST_ATTACHED: return "Host Attached";
	case USB_OTGHOST_DETACHED: return "Host Detached";
	case USB_CABLE_DETACHED_WITHOUT_NOTI:
		return "USB Cable Detached without noti";
	default: return "Unknown cable state";
	}
}


static void c210_otghost_start(struct s3c_udc *dev)
{
	host_notifier_pdata.ndev.mode = NOTIFY_HOST_MODE;
	host_state_notify(&host_notifier_pdata.ndev, NOTIFY_HOST_ADD);

	pr_info("otg start: udc %p, regs %p\n", dev, dev->regs);
	free_irq(IRQ_USB_HSOTG, dev);

	if (otghost_data.start)
		otghost_data.start((u32)dev->regs);
}

static int c210_otghost_stop(struct s3c_udc *dev)
{
	struct s5p_usbgadget_platdata *pdata;
	int ret = 0;

	host_notifier_pdata.ndev.mode = NOTIFY_NONE_MODE;
	host_state_notify(&host_notifier_pdata.ndev, NOTIFY_HOST_REMOVE);

	if (otghost_data.stop)
		otghost_data.stop();

	pdata = (struct s5p_usbgadget_platdata *)
		s3c_device_usbgadget.dev.platform_data;

	pr_info("otg pdata %p, irq_cb %p, irq %p\n",
			pdata, &pdata->udc_irq, pdata->udc_irq);

	if (pdata && pdata->udc_irq) {
		pr_info("otg request_irq irq %p, dev %p\n",
				pdata->udc_irq, dev);

		ret = request_irq(IRQ_USB_HSOTG,
				pdata->udc_irq, 0, "s3c-udc", dev);
		if (ret != 0) {
			pr_info("otg host - can't get irq %i, err %d\n",
					IRQ_USB_HSOTG, ret);
			return -1;
		}
	}

	return ret;
}

static int c210_change_usb_mode(struct s3c_udc *dev, int mode)
{
	pr_info("otg change mode : %s --> %s (%d --> %d) %s\n",
			get_usb_cable_string(usb_status),
			get_usb_cable_string(mode),
			usb_status, mode,
			dev->udc_enabled ? "enabled" : "disabled"
			);

	switch (mode) {
	case USB_CABLE_DETACHED:
		if (dev->udc_enabled)
			usb_gadget_vbus_disconnect(&dev->gadget);
		break;
	case USB_CABLE_ATTACHED:
		if (!dev->udc_enabled)
			usb_gadget_vbus_connect(&dev->gadget);
		break;
	case USB_OTGHOST_ATTACHED:
		c210_otghost_start(dev);
		break;

	case USB_OTGHOST_DETACHED:
		c210_otghost_stop(dev);
		break;
	}
	usb_status = mode;
	return 0;
}

