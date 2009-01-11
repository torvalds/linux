/*
 * drivers/usb/host/ohci-pnx4008.c
 *
 * driver for Philips PNX4008 USB Host
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

#include <mach/hardware.h>
#include <asm/io.h>

#include <mach/platform.h>
#include <mach/irqs.h>
#include <mach/gpio.h>

#define USB_CTRL	IO_ADDRESS(PNX4008_PWRMAN_BASE + 0x64)

/* USB_CTRL bit defines */
#define USB_SLAVE_HCLK_EN	(1 << 24)
#define USB_HOST_NEED_CLK_EN	(1 << 21)

#define USB_OTG_CLK_CTRL	IO_ADDRESS(PNX4008_USB_CONFIG_BASE + 0xFF4)
#define USB_OTG_CLK_STAT	IO_ADDRESS(PNX4008_USB_CONFIG_BASE + 0xFF8)

/* USB_OTG_CLK_CTRL bit defines */
#define AHB_M_CLOCK_ON		(1 << 4)
#define OTG_CLOCK_ON		(1 << 3)
#define I2C_CLOCK_ON		(1 << 2)
#define DEV_CLOCK_ON		(1 << 1)
#define HOST_CLOCK_ON		(1 << 0)

#define USB_OTG_STAT_CONTROL	IO_ADDRESS(PNX4008_USB_CONFIG_BASE + 0x110)

/* USB_OTG_STAT_CONTROL bit defines */
#define TRANSPARENT_I2C_EN	(1 << 7)
#define HOST_EN			(1 << 0)

/* ISP1301 USB transceiver I2C registers */
#define	ISP1301_MODE_CONTROL_1		0x04	/* u8 read, set, +1 clear */

#define	MC1_SPEED_REG		(1 << 0)
#define	MC1_SUSPEND_REG		(1 << 1)
#define	MC1_DAT_SE0		(1 << 2)
#define	MC1_TRANSPARENT		(1 << 3)
#define	MC1_BDIS_ACON_EN	(1 << 4)
#define	MC1_OE_INT_EN		(1 << 5)
#define	MC1_UART_EN		(1 << 6)
#define	MC1_MASK		0x7f

#define	ISP1301_MODE_CONTROL_2		0x12	/* u8 read, set, +1 clear */

#define	MC2_GLOBAL_PWR_DN	(1 << 0)
#define	MC2_SPD_SUSP_CTRL	(1 << 1)
#define	MC2_BI_DI		(1 << 2)
#define	MC2_TRANSP_BDIR0	(1 << 3)
#define	MC2_TRANSP_BDIR1	(1 << 4)
#define	MC2_AUDIO_EN		(1 << 5)
#define	MC2_PSW_EN		(1 << 6)
#define	MC2_EN2V7		(1 << 7)

#define	ISP1301_OTG_CONTROL_1		0x06	/* u8 read, set, +1 clear */
#	define	OTG1_DP_PULLUP		(1 << 0)
#	define	OTG1_DM_PULLUP		(1 << 1)
#	define	OTG1_DP_PULLDOWN	(1 << 2)
#	define	OTG1_DM_PULLDOWN	(1 << 3)
#	define	OTG1_ID_PULLDOWN	(1 << 4)
#	define	OTG1_VBUS_DRV		(1 << 5)
#	define	OTG1_VBUS_DISCHRG	(1 << 6)
#	define	OTG1_VBUS_CHRG		(1 << 7)
#define	ISP1301_OTG_STATUS		0x10	/* u8 readonly */
#	define	OTG_B_SESS_END		(1 << 6)
#	define	OTG_B_SESS_VLD		(1 << 7)

#define ISP1301_I2C_ADDR 0x2C

#define ISP1301_I2C_MODE_CONTROL_1 0x4
#define ISP1301_I2C_MODE_CONTROL_2 0x12
#define ISP1301_I2C_OTG_CONTROL_1 0x6
#define ISP1301_I2C_OTG_CONTROL_2 0x10
#define ISP1301_I2C_INTERRUPT_SOURCE 0x8
#define ISP1301_I2C_INTERRUPT_LATCH 0xA
#define ISP1301_I2C_INTERRUPT_FALLING 0xC
#define ISP1301_I2C_INTERRUPT_RISING 0xE
#define ISP1301_I2C_REG_CLEAR_ADDR 1

struct i2c_driver isp1301_driver;
struct i2c_client *isp1301_i2c_client;

extern int usb_disabled(void);
extern int ocpi_enable(void);

static struct clk *usb_clk;

static const unsigned short normal_i2c[] =
    { ISP1301_I2C_ADDR, ISP1301_I2C_ADDR + 1, I2C_CLIENT_END };

static int isp1301_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	return 0;
}

static int isp1301_remove(struct i2c_client *client)
{
	return 0;
}

const struct i2c_device_id isp1301_id[] = {
	{ "isp1301_pnx", 0 },
	{ }
};

struct i2c_driver isp1301_driver = {
	.driver = {
		.name = "isp1301_pnx",
	},
	.probe = isp1301_probe,
	.remove = isp1301_remove,
	.id_table = isp1301_id,
};

static void i2c_write(u8 buf, u8 subaddr)
{
	char tmpbuf[2];

	tmpbuf[0] = subaddr;	/*register number */
	tmpbuf[1] = buf;	/*register data */
	i2c_master_send(isp1301_i2c_client, &tmpbuf[0], 2);
}

static void isp1301_configure(void)
{
	/* PNX4008 only supports DAT_SE0 USB mode */
	/* PNX4008 R2A requires setting the MAX603 to output 3.6V */
	/* Power up externel charge-pump */

	i2c_write(MC1_DAT_SE0 | MC1_SPEED_REG, ISP1301_I2C_MODE_CONTROL_1);
	i2c_write(~(MC1_DAT_SE0 | MC1_SPEED_REG),
		  ISP1301_I2C_MODE_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR);
	i2c_write(MC2_BI_DI | MC2_PSW_EN | MC2_SPD_SUSP_CTRL,
		  ISP1301_I2C_MODE_CONTROL_2);
	i2c_write(~(MC2_BI_DI | MC2_PSW_EN | MC2_SPD_SUSP_CTRL),
		  ISP1301_I2C_MODE_CONTROL_2 | ISP1301_I2C_REG_CLEAR_ADDR);
	i2c_write(OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN,
		  ISP1301_I2C_OTG_CONTROL_1);
	i2c_write(~(OTG1_DM_PULLDOWN | OTG1_DP_PULLDOWN),
		  ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR);
	i2c_write(0xFF,
		  ISP1301_I2C_INTERRUPT_LATCH | ISP1301_I2C_REG_CLEAR_ADDR);
	i2c_write(0xFF,
		  ISP1301_I2C_INTERRUPT_FALLING | ISP1301_I2C_REG_CLEAR_ADDR);
	i2c_write(0xFF,
		  ISP1301_I2C_INTERRUPT_RISING | ISP1301_I2C_REG_CLEAR_ADDR);

}

static inline void isp1301_vbus_on(void)
{
	i2c_write(OTG1_VBUS_DRV, ISP1301_I2C_OTG_CONTROL_1);
}

static inline void isp1301_vbus_off(void)
{
	i2c_write(OTG1_VBUS_DRV,
		  ISP1301_I2C_OTG_CONTROL_1 | ISP1301_I2C_REG_CLEAR_ADDR);
}

static void pnx4008_start_hc(void)
{
	unsigned long tmp = __raw_readl(USB_OTG_STAT_CONTROL) | HOST_EN;
	__raw_writel(tmp, USB_OTG_STAT_CONTROL);
	isp1301_vbus_on();
}

static void pnx4008_stop_hc(void)
{
	unsigned long tmp;
	isp1301_vbus_off();
	tmp = __raw_readl(USB_OTG_STAT_CONTROL) & ~HOST_EN;
	__raw_writel(tmp, USB_OTG_STAT_CONTROL);
}

static int __devinit ohci_pnx4008_start(struct usb_hcd *hcd)
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

static const struct hc_driver ohci_pnx4008_hc_driver = {
	.description = hcd_name,
	.product_desc =		"pnx4008 OHCI",

	/*
	 * generic hardware linkage
	 */
	.irq = ohci_irq,
	.flags = HCD_USB11 | HCD_MEMORY,

	.hcd_priv_size =	sizeof(struct ohci_hcd),
	/*
	 * basic lifecycle operations
	 */
	.start = ohci_pnx4008_start,
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

#define USB_CLOCK_MASK (AHB_M_CLOCK_ON| OTG_CLOCK_ON | HOST_CLOCK_ON | I2C_CLOCK_ON)

static void pnx4008_set_usb_bits(void)
{
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

static void pnx4008_unset_usb_bits(void)
{
	start_int_mask(SE_USB_OTG_ATX_INT_N);
	start_int_mask(SE_USB_OTG_TIMER_INT);
	start_int_mask(SE_USB_I2C_INT);
	start_int_mask(SE_USB_INT);
	start_int_mask(SE_USB_NEED_CLK_INT);
	start_int_mask(SE_USB_AHB_NEED_CLK_INT);
}

static int __devinit usb_hcd_pnx4008_probe(struct platform_device *pdev)
{
	struct usb_hcd *hcd = 0;
	struct ohci_hcd *ohci;
	const struct hc_driver *driver = &ohci_pnx4008_hc_driver;
	struct i2c_adapter *i2c_adap;
	struct i2c_board_info i2c_info;

	int ret = 0, irq;

	dev_dbg(&pdev->dev, "%s: " DRIVER_DESC " (pnx4008)\n", hcd_name);
	if (usb_disabled()) {
		err("USB is disabled");
		ret = -ENODEV;
		goto out;
	}

	if (pdev->num_resources != 2
	    || pdev->resource[0].flags != IORESOURCE_MEM
	    || pdev->resource[1].flags != IORESOURCE_IRQ) {
		err("Invalid resource configuration");
		ret = -ENODEV;
		goto out;
	}

	/* Enable AHB slave USB clock, needed for further USB clock control */
	__raw_writel(USB_SLAVE_HCLK_EN | (1 << 19), USB_CTRL);

	ret = i2c_add_driver(&isp1301_driver);
	if (ret < 0) {
		err("failed to add ISP1301 driver");
		goto out;
	}
	i2c_adap = i2c_get_adapter(2);
	memset(&i2c_info, 0, sizeof(struct i2c_board_info));
	strlcpy(i2c_info.name, "isp1301_pnx", I2C_NAME_SIZE);
	isp1301_i2c_client = i2c_new_probed_device(i2c_adap, &i2c_info,
						   normal_i2c);
	i2c_put_adapter(i2c_adap);
	if (!isp1301_i2c_client) {
		err("failed to connect I2C to ISP1301 USB Transceiver");
		ret = -ENODEV;
		goto out_i2c_driver;
	}

	isp1301_configure();

	/* Enable USB PLL */
	usb_clk = clk_get(&pdev->dev, "ck_pll5");
	if (IS_ERR(usb_clk)) {
		err("failed to acquire USB PLL");
		ret = PTR_ERR(usb_clk);
		goto out1;
	}

	ret = clk_enable(usb_clk);
	if (ret < 0) {
		err("failed to start USB PLL");
		goto out2;
	}

	ret = clk_set_rate(usb_clk, 48000);
	if (ret < 0) {
		err("failed to set USB clock rate");
		goto out3;
	}

	__raw_writel(__raw_readl(USB_CTRL) | USB_HOST_NEED_CLK_EN, USB_CTRL);

	/* Set to enable all needed USB clocks */
	__raw_writel(USB_CLOCK_MASK, USB_OTG_CLK_CTRL);

	while ((__raw_readl(USB_OTG_CLK_STAT) & USB_CLOCK_MASK) !=
	       USB_CLOCK_MASK) ;

	hcd = usb_create_hcd (driver, &pdev->dev, dev_name(&pdev->dev));
	if (!hcd) {
		err("Failed to allocate HC buffer");
		ret = -ENOMEM;
		goto out3;
	}

	/* Set all USB bits in the Start Enable register */
	pnx4008_set_usb_bits();

	hcd->rsrc_start = pdev->resource[0].start;
	hcd->rsrc_len = pdev->resource[0].end - pdev->resource[0].start + 1;
	if (!request_mem_region(hcd->rsrc_start, hcd->rsrc_len, hcd_name)) {
		dev_dbg(&pdev->dev, "request_mem_region failed\n");
		ret =  -ENOMEM;
		goto out4;
	}
	hcd->regs = (void __iomem *)pdev->resource[0].start;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		ret = -ENXIO;
		goto out4;
	}

	pnx4008_start_hc();
	platform_set_drvdata(pdev, hcd);
	ohci = hcd_to_ohci(hcd);
	ohci_hcd_init(ohci);

	dev_info(&pdev->dev, "at 0x%p, irq %d\n", hcd->regs, hcd->irq);
	ret = usb_add_hcd(hcd, irq, IRQF_DISABLED);
	if (ret == 0)
		return ret;

	pnx4008_stop_hc();
out4:
	pnx4008_unset_usb_bits();
	usb_put_hcd(hcd);
out3:
	clk_disable(usb_clk);
out2:
	clk_put(usb_clk);
out1:
	i2c_unregister_client(isp1301_i2c_client);
	isp1301_i2c_client = NULL;
out_i2c_driver:
	i2c_del_driver(&isp1301_driver);
out:
	return ret;
}

static int usb_hcd_pnx4008_remove(struct platform_device *pdev)
{
	struct usb_hcd *hcd = platform_get_drvdata(pdev);

	usb_remove_hcd(hcd);
	pnx4008_stop_hc();
	release_mem_region(hcd->rsrc_start, hcd->rsrc_len);
	usb_put_hcd(hcd);
	pnx4008_unset_usb_bits();
	clk_disable(usb_clk);
	clk_put(usb_clk);
	i2c_unregister_client(isp1301_i2c_client);
	isp1301_i2c_client = NULL;
	i2c_del_driver(&isp1301_driver);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:usb-ohci");

static struct platform_driver usb_hcd_pnx4008_driver = {
	.driver = {
		.name = "usb-ohci",
		.owner	= THIS_MODULE,
	},
	.probe = usb_hcd_pnx4008_probe,
	.remove = usb_hcd_pnx4008_remove,
};

