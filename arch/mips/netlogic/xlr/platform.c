/*
 * Copyright 2011, Netlogic Microsystems.
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/resource.h>
#include <linux/serial_8250.h>
#include <linux/serial_reg.h>

#include <asm/netlogic/haldefs.h>
#include <asm/netlogic/xlr/iomap.h>
#include <asm/netlogic/xlr/pic.h>
#include <asm/netlogic/xlr/xlr.h>

unsigned int nlm_xlr_uart_in(struct uart_port *p, int offset)
{
	uint64_t uartbase;
	unsigned int value;

	/* sign extend to 64 bits, if needed */
	uartbase = (uint64_t)(long)p->membase;
	value = nlm_read_reg(uartbase, offset);

	/* See XLR/XLS errata */
	if (offset == UART_MSR)
		value ^= 0xF0;
	else if (offset == UART_MCR)
		value ^= 0x3;

	return value;
}

void nlm_xlr_uart_out(struct uart_port *p, int offset, int value)
{
	uint64_t uartbase;

	/* sign extend to 64 bits, if needed */
	uartbase = (uint64_t)(long)p->membase;

	/* See XLR/XLS errata */
	if (offset == UART_MSR)
		value ^= 0xF0;
	else if (offset == UART_MCR)
		value ^= 0x3;

	nlm_write_reg(uartbase, offset, value);
}

#define PORT(_irq)					\
	{						\
		.irq		= _irq,			\
		.regshift	= 2,			\
		.iotype		= UPIO_MEM32,		\
		.flags		= (UPF_SKIP_TEST |	\
			 UPF_FIXED_TYPE | UPF_BOOT_AUTOCONF),\
		.uartclk	= PIC_CLKS_PER_SEC,	\
		.type		= PORT_16550A,		\
		.serial_in	= nlm_xlr_uart_in,	\
		.serial_out	= nlm_xlr_uart_out,	\
	}

static struct plat_serial8250_port xlr_uart_data[] = {
	PORT(PIC_UART_0_IRQ),
	PORT(PIC_UART_1_IRQ),
	{},
};

static struct platform_device uart_device = {
	.name		= "serial8250",
	.id		= PLAT8250_DEV_PLATFORM,
	.dev = {
		.platform_data = xlr_uart_data,
	},
};

static int __init nlm_uart_init(void)
{
	unsigned long uartbase;

	uartbase = (unsigned long)nlm_mmio_base(NETLOGIC_IO_UART_0_OFFSET);
	xlr_uart_data[0].membase = (void __iomem *)uartbase;
	xlr_uart_data[0].mapbase = CPHYSADDR(uartbase);

	uartbase = (unsigned long)nlm_mmio_base(NETLOGIC_IO_UART_1_OFFSET);
	xlr_uart_data[1].membase = (void __iomem *)uartbase;
	xlr_uart_data[1].mapbase = CPHYSADDR(uartbase);

	return platform_device_register(&uart_device);
}

arch_initcall(nlm_uart_init);

#ifdef CONFIG_USB
/* Platform USB devices, only on XLS chips */
static u64 xls_usb_dmamask = ~(u32)0;
#define USB_PLATFORM_DEV(n, i, irq)					\
	{								\
		.name		= n,					\
		.id		= i,					\
		.num_resources	= 2,					\
		.dev		= {					\
			.dma_mask	= &xls_usb_dmamask,		\
			.coherent_dma_mask = 0xffffffff,		\
		},							\
		.resource	= (struct resource[]) {			\
			{						\
				.flags = IORESOURCE_MEM,		\
			},						\
			{						\
				.start	= irq,				\
				.end	= irq,				\
				.flags = IORESOURCE_IRQ,		\
			},						\
		},							\
	}

static struct platform_device xls_usb_ehci_device =
			 USB_PLATFORM_DEV("ehci-xls", 0, PIC_USB_IRQ);
static struct platform_device xls_usb_ohci_device_0 =
			 USB_PLATFORM_DEV("ohci-xls-0", 1, PIC_USB_IRQ);
static struct platform_device xls_usb_ohci_device_1 =
			 USB_PLATFORM_DEV("ohci-xls-1", 2, PIC_USB_IRQ);

static struct platform_device *xls_platform_devices[] = {
	&xls_usb_ehci_device,
	&xls_usb_ohci_device_0,
	&xls_usb_ohci_device_1,
};

int xls_platform_usb_init(void)
{
	uint64_t usb_mmio, gpio_mmio;
	unsigned long memres;
	uint32_t val;

	if (!nlm_chip_is_xls())
		return 0;

	gpio_mmio = nlm_mmio_base(NETLOGIC_IO_GPIO_OFFSET);
	usb_mmio  = nlm_mmio_base(NETLOGIC_IO_USB_1_OFFSET);

	/* Clear Rogue Phy INTs */
	nlm_write_reg(usb_mmio, 49, 0x10000000);
	/* Enable all interrupts */
	nlm_write_reg(usb_mmio, 50, 0x1f000000);

	/* Enable ports */
	nlm_write_reg(usb_mmio,  1, 0x07000500);

	val = nlm_read_reg(gpio_mmio, 21);
	if (((val >> 22) & 0x01) == 0) {
		pr_info("Detected USB Device mode - Not supported!\n");
		nlm_write_reg(usb_mmio,  0, 0x01000000);
		return 0;
	}

	pr_info("Detected USB Host mode - Adding XLS USB devices.\n");
	/* Clear reset, host mode */
	nlm_write_reg(usb_mmio,  0, 0x02000000);

	/* Memory resource for various XLS usb ports */
	usb_mmio = nlm_mmio_base(NETLOGIC_IO_USB_0_OFFSET);
	memres = CPHYSADDR((unsigned long)usb_mmio);
	xls_usb_ehci_device.resource[0].start = memres;
	xls_usb_ehci_device.resource[0].end = memres + 0x400 - 1;

	memres += 0x400;
	xls_usb_ohci_device_0.resource[0].start = memres;
	xls_usb_ohci_device_0.resource[0].end = memres + 0x400 - 1;

	memres += 0x400;
	xls_usb_ohci_device_1.resource[0].start = memres;
	xls_usb_ohci_device_1.resource[0].end = memres + 0x400 - 1;

	return platform_add_devices(xls_platform_devices,
				ARRAY_SIZE(xls_platform_devices));
}

arch_initcall(xls_platform_usb_init);
#endif
