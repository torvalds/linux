/*
 * Goramo MultiLink router platform code
 * Copyright (C) 2006-2009 Krzysztof Halasa <khc@pm.waw.pl>
 */

#include <linux/delay.h>
#include <linux/hdlc.h>
#include <linux/i2c-gpio.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/serial_8250.h>
#include <asm/mach-types.h>
#include <asm/system.h>
#include <asm/mach/arch.h>
#include <asm/mach/flash.h>
#include <asm/mach/pci.h>

#define SLOT_ETHA		0x0B	/* IDSEL = AD21 */
#define SLOT_ETHB		0x0C	/* IDSEL = AD20 */
#define SLOT_MPCI		0x0D	/* IDSEL = AD19 */
#define SLOT_NEC		0x0E	/* IDSEL = AD18 */

/* GPIO lines */
#define GPIO_SCL		0
#define GPIO_SDA		1
#define GPIO_STR		2
#define GPIO_IRQ_NEC		3
#define GPIO_IRQ_ETHA		4
#define GPIO_IRQ_ETHB		5
#define GPIO_HSS0_DCD_N		6
#define GPIO_HSS1_DCD_N		7
#define GPIO_UART0_DCD		8
#define GPIO_UART1_DCD		9
#define GPIO_HSS0_CTS_N		10
#define GPIO_HSS1_CTS_N		11
#define GPIO_IRQ_MPCI		12
#define GPIO_HSS1_RTS_N		13
#define GPIO_HSS0_RTS_N		14
/* GPIO15 is not connected */

/* Control outputs from 74HC4094 */
#define CONTROL_HSS0_CLK_INT	0
#define CONTROL_HSS1_CLK_INT	1
#define CONTROL_HSS0_DTR_N	2
#define CONTROL_HSS1_DTR_N	3
#define CONTROL_EXT		4
#define CONTROL_AUTO_RESET	5
#define CONTROL_PCI_RESET_N	6
#define CONTROL_EEPROM_WC_N	7

/* offsets from start of flash ROM = 0x50000000 */
#define CFG_ETH0_ADDRESS	0x40 /* 6 bytes */
#define CFG_ETH1_ADDRESS	0x46 /* 6 bytes */
#define CFG_REV			0x4C /* u32 */
#define CFG_SDRAM_SIZE		0x50 /* u32 */
#define CFG_SDRAM_CONF		0x54 /* u32 */
#define CFG_SDRAM_MODE		0x58 /* u32 */
#define CFG_SDRAM_REFRESH	0x5C /* u32 */

#define CFG_HW_BITS		0x60 /* u32 */
#define  CFG_HW_USB_PORTS	0x00000007 /* 0 = no NEC chip, 1-5 = ports # */
#define  CFG_HW_HAS_PCI_SLOT	0x00000008
#define  CFG_HW_HAS_ETH0	0x00000010
#define  CFG_HW_HAS_ETH1	0x00000020
#define  CFG_HW_HAS_HSS0	0x00000040
#define  CFG_HW_HAS_HSS1	0x00000080
#define  CFG_HW_HAS_UART0	0x00000100
#define  CFG_HW_HAS_UART1	0x00000200
#define  CFG_HW_HAS_EEPROM	0x00000400

#define FLASH_CMD_READ_ARRAY	0xFF
#define FLASH_CMD_READ_ID	0x90
#define FLASH_SER_OFF		0x102 /* 0x81 in 16-bit mode */

static u32 hw_bits = 0xFFFFFFFD;    /* assume all hardware present */;
static u8 control_value;

static void set_scl(u8 value)
{
	gpio_line_set(GPIO_SCL, !!value);
	udelay(3);
}

static void set_sda(u8 value)
{
	gpio_line_set(GPIO_SDA, !!value);
	udelay(3);
}

static void set_str(u8 value)
{
	gpio_line_set(GPIO_STR, !!value);
	udelay(3);
}

static inline void set_control(int line, int value)
{
	if (value)
		control_value |=  (1 << line);
	else
		control_value &= ~(1 << line);
}


static void output_control(void)
{
	int i;

	gpio_line_config(GPIO_SCL, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_SDA, IXP4XX_GPIO_OUT);

	for (i = 0; i < 8; i++) {
		set_scl(0);
		set_sda(control_value & (0x80 >> i)); /* MSB first */
		set_scl(1);	/* active edge */
	}

	set_str(1);
	set_str(0);

	set_scl(0);
	set_sda(1);		/* Be ready for START */
	set_scl(1);
}


static void (*set_carrier_cb_tab[2])(void *pdev, int carrier);

static int hss_set_clock(int port, unsigned int clock_type)
{
	int ctrl_int = port ? CONTROL_HSS1_CLK_INT : CONTROL_HSS0_CLK_INT;

	switch (clock_type) {
	case CLOCK_DEFAULT:
	case CLOCK_EXT:
		set_control(ctrl_int, 0);
		output_control();
		return CLOCK_EXT;

	case CLOCK_INT:
		set_control(ctrl_int, 1);
		output_control();
		return CLOCK_INT;

	default:
		return -EINVAL;
	}
}

static irqreturn_t hss_dcd_irq(int irq, void *pdev)
{
	int i, port = (irq == IXP4XX_GPIO_IRQ(GPIO_HSS1_DCD_N));
	gpio_line_get(port ? GPIO_HSS1_DCD_N : GPIO_HSS0_DCD_N, &i);
	set_carrier_cb_tab[port](pdev, !i);
	return IRQ_HANDLED;
}


static int hss_open(int port, void *pdev,
		    void (*set_carrier_cb)(void *pdev, int carrier))
{
	int i, irq;

	if (!port)
		irq = IXP4XX_GPIO_IRQ(GPIO_HSS0_DCD_N);
	else
		irq = IXP4XX_GPIO_IRQ(GPIO_HSS1_DCD_N);

	gpio_line_get(port ? GPIO_HSS1_DCD_N : GPIO_HSS0_DCD_N, &i);
	set_carrier_cb(pdev, !i);

	set_carrier_cb_tab[!!port] = set_carrier_cb;

	if ((i = request_irq(irq, hss_dcd_irq, 0, "IXP4xx HSS", pdev)) != 0) {
		printk(KERN_ERR "ixp4xx_hss: failed to request IRQ%i (%i)\n",
		       irq, i);
		return i;
	}

	set_control(port ? CONTROL_HSS1_DTR_N : CONTROL_HSS0_DTR_N, 0);
	output_control();
	gpio_line_set(port ? GPIO_HSS1_RTS_N : GPIO_HSS0_RTS_N, 0);
	return 0;
}

static void hss_close(int port, void *pdev)
{
	free_irq(port ? IXP4XX_GPIO_IRQ(GPIO_HSS1_DCD_N) :
		 IXP4XX_GPIO_IRQ(GPIO_HSS0_DCD_N), pdev);
	set_carrier_cb_tab[!!port] = NULL; /* catch bugs */

	set_control(port ? CONTROL_HSS1_DTR_N : CONTROL_HSS0_DTR_N, 1);
	output_control();
	gpio_line_set(port ? GPIO_HSS1_RTS_N : GPIO_HSS0_RTS_N, 1);
}


/* Flash memory */
static struct flash_platform_data flash_data = {
	.map_name	= "cfi_probe",
	.width		= 2,
};

static struct resource flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device device_flash = {
	.name		= "IXP4XX-Flash",
	.id		= 0,
	.dev		= { .platform_data = &flash_data },
	.num_resources	= 1,
	.resource	= &flash_resource,
};


/* I^2C interface */
static struct i2c_gpio_platform_data i2c_data = {
	.sda_pin	= GPIO_SDA,
	.scl_pin	= GPIO_SCL,
};

static struct platform_device device_i2c = {
	.name		= "i2c-gpio",
	.id		= 0,
	.dev		= { .platform_data = &i2c_data },
};


/* IXP425 2 UART ports */
static struct resource uart_resources[] = {
	{
		.start		= IXP4XX_UART1_BASE_PHYS,
		.end		= IXP4XX_UART1_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= IXP4XX_UART2_BASE_PHYS,
		.end		= IXP4XX_UART2_BASE_PHYS + 0x0fff,
		.flags		= IORESOURCE_MEM,
	}
};

static struct plat_serial8250_port uart_data[] = {
	{
		.mapbase	= IXP4XX_UART1_BASE_PHYS,
		.membase	= (char __iomem *)IXP4XX_UART1_BASE_VIRT +
			REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART1,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{
		.mapbase	= IXP4XX_UART2_BASE_PHYS,
		.membase	= (char __iomem *)IXP4XX_UART2_BASE_VIRT +
			REG_OFFSET,
		.irq		= IRQ_IXP4XX_UART2,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IXP4XX_UART_XTAL,
	},
	{ },
};

static struct platform_device device_uarts = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev.platform_data	= uart_data,
	.num_resources		= 2,
	.resource		= uart_resources,
};


/* Built-in 10/100 Ethernet MAC interfaces */
static struct eth_plat_info eth_plat[] = {
	{
		.phy		= 0,
		.rxq		= 3,
		.txreadyq	= 32,
	}, {
		.phy		= 1,
		.rxq		= 4,
		.txreadyq	= 33,
	}
};

static struct platform_device device_eth_tab[] = {
	{
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEB,
		.dev.platform_data	= eth_plat,
	}, {
		.name			= "ixp4xx_eth",
		.id			= IXP4XX_ETH_NPEC,
		.dev.platform_data	= eth_plat + 1,
	}
};


/* IXP425 2 synchronous serial ports */
static struct hss_plat_info hss_plat[] = {
	{
		.set_clock	= hss_set_clock,
		.open		= hss_open,
		.close		= hss_close,
		.txreadyq	= 34,
	}, {
		.set_clock	= hss_set_clock,
		.open		= hss_open,
		.close		= hss_close,
		.txreadyq	= 35,
	}
};

static struct platform_device device_hss_tab[] = {
	{
		.name			= "ixp4xx_hss",
		.id			= 0,
		.dev.platform_data	= hss_plat,
	}, {
		.name			= "ixp4xx_hss",
		.id			= 1,
		.dev.platform_data	= hss_plat + 1,
	}
};


static struct platform_device *device_tab[6] __initdata = {
	&device_flash,		/* index 0 */
};

static inline u8 __init flash_readb(u8 __iomem *flash, u32 addr)
{
#ifdef __ARMEB__
	return __raw_readb(flash + addr);
#else
	return __raw_readb(flash + (addr ^ 3));
#endif
}

static inline u16 __init flash_readw(u8 __iomem *flash, u32 addr)
{
#ifdef __ARMEB__
	return __raw_readw(flash + addr);
#else
	return __raw_readw(flash + (addr ^ 2));
#endif
}

static void __init gmlr_init(void)
{
	u8 __iomem *flash;
	int i, devices = 1; /* flash */

	ixp4xx_sys_init();

	if ((flash = ioremap(IXP4XX_EXP_BUS_BASE_PHYS, 0x80)) == NULL)
		printk(KERN_ERR "goramo-mlr: unable to access system"
		       " configuration data\n");
	else {
		system_rev = __raw_readl(flash + CFG_REV);
		hw_bits = __raw_readl(flash + CFG_HW_BITS);

		for (i = 0; i < ETH_ALEN; i++) {
			eth_plat[0].hwaddr[i] =
				flash_readb(flash, CFG_ETH0_ADDRESS + i);
			eth_plat[1].hwaddr[i] =
				flash_readb(flash, CFG_ETH1_ADDRESS + i);
		}

		__raw_writew(FLASH_CMD_READ_ID, flash);
		system_serial_high = flash_readw(flash, FLASH_SER_OFF);
		system_serial_high <<= 16;
		system_serial_high |= flash_readw(flash, FLASH_SER_OFF + 2);
		system_serial_low = flash_readw(flash, FLASH_SER_OFF + 4);
		system_serial_low <<= 16;
		system_serial_low |= flash_readw(flash, FLASH_SER_OFF + 6);
		__raw_writew(FLASH_CMD_READ_ARRAY, flash);

		iounmap(flash);
	}

	switch (hw_bits & (CFG_HW_HAS_UART0 | CFG_HW_HAS_UART1)) {
	case CFG_HW_HAS_UART0:
		memset(&uart_data[1], 0, sizeof(uart_data[1]));
		device_uarts.num_resources = 1;
		break;

	case CFG_HW_HAS_UART1:
		device_uarts.dev.platform_data = &uart_data[1];
		device_uarts.resource = &uart_resources[1];
		device_uarts.num_resources = 1;
		break;
	}
	if (hw_bits & (CFG_HW_HAS_UART0 | CFG_HW_HAS_UART1))
		device_tab[devices++] = &device_uarts; /* max index 1 */

	if (hw_bits & CFG_HW_HAS_ETH0)
		device_tab[devices++] = &device_eth_tab[0]; /* max index 2 */
	if (hw_bits & CFG_HW_HAS_ETH1)
		device_tab[devices++] = &device_eth_tab[1]; /* max index 3 */

	if (hw_bits & CFG_HW_HAS_HSS0)
		device_tab[devices++] = &device_hss_tab[0]; /* max index 4 */
	if (hw_bits & CFG_HW_HAS_HSS1)
		device_tab[devices++] = &device_hss_tab[1]; /* max index 5 */

	if (hw_bits & CFG_HW_HAS_EEPROM)
		device_tab[devices++] = &device_i2c; /* max index 6 */

	gpio_line_config(GPIO_SCL, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_SDA, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_STR, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_HSS0_RTS_N, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_HSS1_RTS_N, IXP4XX_GPIO_OUT);
	gpio_line_config(GPIO_HSS0_DCD_N, IXP4XX_GPIO_IN);
	gpio_line_config(GPIO_HSS1_DCD_N, IXP4XX_GPIO_IN);
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_HSS0_DCD_N), IRQ_TYPE_EDGE_BOTH);
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_HSS1_DCD_N), IRQ_TYPE_EDGE_BOTH);

	set_control(CONTROL_HSS0_DTR_N, 1);
	set_control(CONTROL_HSS1_DTR_N, 1);
	set_control(CONTROL_EEPROM_WC_N, 1);
	set_control(CONTROL_PCI_RESET_N, 1);
	output_control();

	msleep(1);	      /* Wait for PCI devices to initialize */

	flash_resource.start = IXP4XX_EXP_BUS_BASE(0);
	flash_resource.end = IXP4XX_EXP_BUS_BASE(0) + ixp4xx_exp_bus_size - 1;

	platform_add_devices(device_tab, devices);
}


#ifdef CONFIG_PCI
static void __init gmlr_pci_preinit(void)
{
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_IRQ_ETHA), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_IRQ_ETHB), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_IRQ_NEC), IRQ_TYPE_LEVEL_LOW);
	set_irq_type(IXP4XX_GPIO_IRQ(GPIO_IRQ_MPCI), IRQ_TYPE_LEVEL_LOW);
	ixp4xx_pci_preinit();
}

static void __init gmlr_pci_postinit(void)
{
	if ((hw_bits & CFG_HW_USB_PORTS) >= 2 &&
	    (hw_bits & CFG_HW_USB_PORTS) < 5) {
		/* need to adjust number of USB ports on NEC chip */
		u32 value, addr = BIT(32 - SLOT_NEC) | 0xE0;
		if (!ixp4xx_pci_read(addr, NP_CMD_CONFIGREAD, &value)) {
			value &= ~7;
			value |= (hw_bits & CFG_HW_USB_PORTS);
			ixp4xx_pci_write(addr, NP_CMD_CONFIGWRITE, value);
		}
	}
}

static int __init gmlr_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	switch(slot) {
	case SLOT_ETHA:	return IXP4XX_GPIO_IRQ(GPIO_IRQ_ETHA);
	case SLOT_ETHB:	return IXP4XX_GPIO_IRQ(GPIO_IRQ_ETHB);
	case SLOT_NEC:	return IXP4XX_GPIO_IRQ(GPIO_IRQ_NEC);
	default:	return IXP4XX_GPIO_IRQ(GPIO_IRQ_MPCI);
	}
}

static struct hw_pci gmlr_hw_pci __initdata = {
	.nr_controllers = 1,
	.preinit	= gmlr_pci_preinit,
	.postinit	= gmlr_pci_postinit,
	.swizzle	= pci_std_swizzle,
	.setup		= ixp4xx_setup,
	.scan		= ixp4xx_scan_bus,
	.map_irq	= gmlr_map_irq,
};

static int __init gmlr_pci_init(void)
{
	if (machine_is_goramo_mlr() &&
	    (hw_bits & (CFG_HW_USB_PORTS | CFG_HW_HAS_PCI_SLOT)))
		pci_common_init(&gmlr_hw_pci);
	return 0;
}

subsys_initcall(gmlr_pci_init);
#endif /* CONFIG_PCI */


MACHINE_START(GORAMO_MLR, "MultiLink")
	/* Maintainer: Krzysztof Halasa */
	.map_io		= ixp4xx_map_io,
	.init_irq	= ixp4xx_init_irq,
	.timer		= &ixp4xx_timer,
	.boot_params	= 0x0100,
	.init_machine	= gmlr_init,
MACHINE_END
