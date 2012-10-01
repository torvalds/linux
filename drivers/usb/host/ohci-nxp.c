/*
 * driver for NXP USB Host devices
 *
 * Currently supported OHCI host devices:
 * - Philips PNX4008
 * - NXP LPC32xx
 *
 * Authors: Dmitry Chigirev <source@mvista.com>
 *	    Vitaly Wool <vitalywool@gmail.com>
 *
 * register initialization is based on code examples provided by Philips
 * Copyright (c) 2005 Koninklijke Philips Electronics N.V.
 *
 * NOTE: This driver does not have suspend/resume functionality
 * This driver is intended for engineering development purposes only
 *
 * 2005-2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/usb/isp1301.h>

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/io.h>

#include <mach/platform.h>
#include <mach/irqs.h>

#define USB_CONFIG_BASE		0x31020000
#define PWRMAN_BASE		0x40004000

#define USB_CTRL		IO_ADDRESS(PWRMAN_BASE + 0x64)

/* USB_CTRL bit defines */
#define USB_SLAVE_HCLK_EN	(1 << 24)
#define USB_DEV_NEED_CLK_EN	(1 << 22)
#define USB_HOST_NEED_CLK_EN	(1 << 21)
#define PAD_CONTROL_LAST_DRIVEN	(1 << 19)

#define USB_OTG_STAT_CONTROL	IO_ADDRESS(USB_CONFIG_BASE + 0x110)

/* USB_OTG_STAT_CONTROL bit defines */
#define TRANSPARENT_I2C_EN	(1 << 7)
#define HOST_EN			(1 << 0)

/* On LPC32xx, those are undefined */
#ifndef start_int_set_falling_edge
#define start_int_set_falling_edge(irq)
#define start_int_set_rising_edge(irq)
#define start_int_ack(irq)
#define start_int_mask(irq)
#define start_int_umask(irq)
#endif

static struct i2c_client *isp1301_i2c_client;

extern int usb_disabled(void);

static struct clk *usb_pll_clk;
static struct clk *usb_dev_clk;
static struct clk *usb_otg_clk;

static void isp1301_configure_pnx4008(void)
{
	/* PNX4008 only supports DAT_SE0 USB mode */
	/* PNX4008 R2A requires setting the MAX603 to output 3.6V */
	/* Power up externel charge-pump */

	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_1, MC1_DAT_SE0 | MC1_SPEED_REG);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR,
		~(MC1_DAT_SE0 | MC1_SPEED_REG));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_2,
		MC2_BI_DI | MC2_PSW_EN | MC2_SPD_SUSP_CTRL);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_2 | ISP1301_I2C_REG_CLEAR_ADDR,
		~(MC2_BI_DI | MC2_PSW_EN | MC2_SPD_SUSP_CTRL));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_OTG_CONTROL_1, OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR,
		~(OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_LATCH | ISP1301_I2C_REG_CLEAR_ADDR, 0xFF);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_FALLING | ISP1301_I2C_REG_CLEAR_ADDR,
		0xFF);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_RISING | ISP1301_I2C_REG_CLEAR_ADDR,
		0xFF);
}

static void isp1301_configure_lpc32xx(void)
{
	/* LPC32XX only supports DAT_SE0 USB mode */
	/* This sequence is important */

	/* Disable transparent UART mode first */
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		(ISP1301_I2C_MODE_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR),
		MC1_UART_EN);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		(ISP1301_I2C_MODE_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR),
		~MC1_SPEED_REG);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
				  ISP1301_I2C_MODE_CONTROL_1, MC1_SPEED_REG);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		  (ISP1301_I2C_MODE_CONTROL_2 | ISP1301_I2C_REG_CLEAR_ADDR),
		  ~0);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_2,
		(MC2_BI_DI | MC2_PSW_EN | MC2_SPD_SUSP_CTRL));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		(ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR), ~0);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_MODE_CONTROL_1, MC1_DAT_SE0);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_OTG_CONTROL_1,
		(OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		(ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR),
		(OTG1_DM_PULLUP | OTG1_DP_PULLUP));
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_LATCH | ISP1301_I2C_REG_CLEAR_ADDR, ~0);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_FALLING | ISP1301_I2C_REG_CLEAR_ADDR,
		~0);
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_INTERRUPT_RISING | ISP1301_I2C_REG_CLEAR_ADDR, ~0);

	/* Enable usb_need_clk clock after transceiver is initialized */
	__raw_writel(__raw_readl(USB_CTRL) | USB_HOST_NEED_CLK_EN, USB_CTRL);

	printk(KERN_INFO "ISP1301 Vendor ID  : 0x%04x\n",
	      i2c_smbus_read_word_data(isp1301_i2c_client, 0x00));
	printk(KERN_INFO "ISP1301 Product ID : 0x%04x\n",
	      i2c_smbus_read_word_data(isp1301_i2c_client, 0x02));
	printk(KERN_INFO "ISP1301 Version ID : 0x%04x\n",
	      i2c_smbus_read_word_data(isp1301_i2c_client, 0x14));
}

static void isp1301_configure(void)
{
	if (machine_is_pnx4008())
		isp1301_configure_pnx4008();
	else
		isp1301_configure_lpc32xx();
}

static inline void isp1301_vbus_on(void)
{
	i2c_smbus_write_byte_data(isp1301_i2c_client, ISP1301_I2C_OTG_CONTROL_1,
				  OTG1_VBUS_DRV);
}

static inline void isp1301_vbus_off(void)
{
	i2c_smbus_write_byte_data(isp1301_i2c_client,
		ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR,
		OTG1_VBUS_DRV);
}

static void nxp_start_hc(void)
{
	unsigned long tmp = __raw_readl(USB_OTG_STAT_CONTROL) | HOST_EN;
	__raw_writel(tmp, USB_OTG_STAT_CONTROL);
	isp1301_vbus_on();
}

static void nxp_stop_hc(void)
{
	unsigned long tmp;
	isp1301_vbus_off();
	tmp = __raw_readl(USB_OTG_STAT_CONTROL) & ~HOST_EN;
	__raw_writel(tmp, USB_OTG_STAT_CONTROL);
}

static int __devinit ohci_nxp_start(struct usb_hcd *hcd)
{
	struct ohci_hcd *ohci = hcd_to_ohci(hcd);
	int ret;

	if ((ret = ohci_init(ohci)) < 0)
		return ret;

	if ((ret = ohci_run(ohci)) < 0) {
		dev_err(hcd->self.controller, "can't start\n");
		ohci_stop(hcd);
		return ret;
	}
	return 0;
}

static const struct hc_driver ohci_nxp_hc_driver = {
	.description = hcd_name,
	.product_desc =		"nxp OHCI",

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	.hcd_priv_size =	sizeof(struct ohci_hcd),
	/*
	 * basic lifecycle operations
	 */
	.start = ohci_nxp_start,
	.stop = ohci_stop,
	.shutdown = ohci_shutdown,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue = ohci_urb_enqueue,
	.urb_dequeue = ohci_urb_dequeue,
	.endpoint_disable = ohci_endpoint_disable,

	/*
	 * scheduling support
	 */
	.get_frame_number = ohci_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data = ohci_hub_status_data,
	.hub_control = ohci_hub_control,
#ifdef	CONFIG_PM
	.bus_suspend = ohci_bus_suspend,
	.bus_resume = ohci_bus_resume,
#endif
	.start_port_reset = ohci_start_port_reset,
};

static void nxp_set_usb_bits(void)
{
	if (machine_is_pnx4008()) {
		start_int_set_falling_edge(SE_USB_OTG_ATX_INT_N);
		start_int_ack(SE_USB_OTG_ATX_INT_N);
		start_int_umask(SE_USB_OTG_ATX_INT_N);

		start_int_set_rising_edge(SE_USB_OTG_TIMER_INT);
		start_int_ack(SE_USB_OTG_TIMER_INT);
		start_int_umask(SE_USB_OTG_TIMER_INT);

		start_int_set_rising_edge(SE_USB_I2C_INT);
		start_int_ack(SE_USB_I2C_INT);
		start_int_umask(SE_USB_I2C_INT);

		start_int_set_rising_edge(SE_USB_INT);
		start_int_ack(SE_USB_INT);
		start_int_umask(SE_USB_INT);

		start_int_set_rising_edge(SE_USB_NEED_CLK_INT);
		start_int_ack(SE_USB_NEED_CLK_INT);
		start_int_umask(SE_USB_NEED_CLK_INT);

		start_int_set_rising_edge(SE_USB_AHB_NEED_CLK_INT);
		start_int_ack(SE_USB_AHB_NEED_CLK_INT);
		start_int_umask(SE_USB_AHB_NEED_CLK_INT);
	}
}

static void nxp_unset_usb_bits(void)
{
	if (machine_is_pnx4008()) {
		start_int_mask(SE_USB_OTG_ATX_INT_N);
		start_int_mask(SE_USB_OTG_TIMER_INT);
		start_int_mask(SE_USB_I2C_INT);
		start_int_mask(SE_USB_INT);
		start_int_mask(SE_USB_NEED_CLK_INT);
		start_int_mask(SE_USB_AHB_NEED_CLK_INT);
	}
}

static int __devinit usb_hcd_nxp_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = 0;
	struct ohci_hcd *ohci;
	const struct hc_driver *driver = &ohci_nxp_hc_driver;
	struct resource *res;
	int ret = 0, irq;
	struct device_node *isp1301_node;

	if (pdev->dev.of_node) {
		isp1301_node = of_parse_phandle(pdev->dev.of_node,
						"transceiver", 0);
	} else {
		isp1301_node = NULL;
	}

	isp1301_i2c_client = isp1301_get_client(isp1301_node);
	if (!isp1301_i2c_client) {
		ret = -EPROBE_DEFER;
		goto out;
	}

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	pdev->dev.dma_mask = &pdev->dev.coherent_dma_mask;

	dev_dbg(&pdev->dev, "%s: " DRIVER_DESC " (nxp)\n", hcd_name);
	if (usb_disabled()) {
		dev_err(&pdev->dev, "USB is disabled\n");
		ret = -ENODEV;
		goto out;
	}

	/* Enable AHB slave USB clock, needed for further USB clock control */
	__raw_writel(USB_SLAVE_HCLK_EN | PAD_CONTROL_LAST_DRIVEN, USB_CTRL);

	/* Enable USB PLL */
	usb_pll_clk = clk_get(&pdev->dev, "ck_pll5");
	if (IS_ERR(usb_pll_clk)) {
		dev_err(&pdev->dev, "failed to acquire USB PLL\n");
		ret = PTR_ERR(usb_pll_clk);
		goto out1;
	}

	ret = clk_enable(usb_pll_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to start USB PLL\n");
		goto out2;
	}

	ret = clk_set_rate(usb_pll_clk, 48000);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to set USB clock rate\n");
		goto out3;
	}

	/* Enable USB device clock */
	usb_dev_clk = clk_get(&pdev->dev, "ck_usbd");
	if (IS_ERR(usb_dev_clk)) {
		dev_err(&pdev->dev, "failed to acquire USB DEV Clock\n");
		ret = PTR_ERR(usb_dev_clk);
		goto out4;
	}

	ret = clk_enable(usb_dev_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to start USB DEV Clock\n");
		goto out5;
	}

	/* Enable USB otg clocks */
	usb_otg_clk = clk_get(&pdev->dev, "ck_usb_otg");
	if (IS_ERR(usb_otg_clk)) {
		dev_err(&pdev->dev, "failed to acquire USB DEV Clock\n");
		ret = PTR_ERR(usb_otg_clk);
		goto out6;
	}

	__raw_writel(__raw_readl(USB_CTRL) | USB_HOST_NEED_CLK_EN, USB_CTRL);

	ret = clk_enable(usb_otg_clk);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to start USB DEV Clock\n");
		goto out7;
	}

	isp1301_configure();

	hcd = usb_create_hcd(driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		dev_err(&pdev->dev, "Failed to allocate HC buffer\n");
		ret = -ENOMEM;
		goto out8;
	}

	/* Set all USB bits in the Start Enable register */
	nxp_set_usb_bits();

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "Failed to get MEM resource\n");
		ret =  -ENOMEM;
		goto out8;
	}

	hcd->regs = devm_request_and_ioremap(&pdev->dev, res);
	if (!hcd->regs) {
		dev_err(&pdev->dev, "Failed to devm_request_and_ioremap\n");
		ret =  -ENOMEM;
		goto out8;
	}
	hcd->rsrc_start = res->start;
	hcd->rsrc_len = resource_size(res);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto out8;
	}

	nxp_start_hc();
	platform_set_drvdata(pdev, hcd);
	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	dev_info(&pdev->dev, "at 0x%p, irq %d\n", hcd->regs, hcd->irq);
	ret = usb_add_hcd(hcd, irq, 0);
	if (ret == 0)
		return ret;

	nxp_stop_hc();
out8:
	nxp_unset_usb_bits();
	usb_put_hcd(hcd);
out7:
	clk_disable(usb_otg_clk);
out6:
	clk_put(usb_otg_clk);
out5:
	clk_disable(usb_dev_clk);
out4:
	clk_put(usb_dev_clk);
out3:
	clk_disable(usb_pll_clk);
out2:
	clk_put(usb_pll_clk);
out1:
	isp1301_i2c_client = NULL;
out:
	return ret;
}

static int usb_hcd_nxp_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	nxp_stop_hc();
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	nxp_unset_usb_bits();
	clk_disable(usb_pll_clk);
	clk_put(usb_pll_clk);
	clk_disable(usb_dev_clk);
	clk_put(usb_dev_clk);
	i2c_unregister_device(isp1301_i2c_client);
	isp1301_i2c_client = NULL;

	platform_set_drvdata(pdev, NULL);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:usb-ohci");

#ifdef CONFIG_OF
static const struct of_device_id usb_hcd_nxp_match[] = {
	{ .compatible = "nxp,ohci-nxp" },
	{},
};
MODULE_DEVICE_TABLE(of, usb_hcd_nxp_match);
#endif

static struct platform_driver usb_hcd_nxp_driver = {
	.driver = {
		.name = "usb-ohci",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(usb_hcd_nxp_match),
	},
	.probe = usb_hcd_nxp_probe,
	.remove = usb_hcd_nxp_remove,
};

