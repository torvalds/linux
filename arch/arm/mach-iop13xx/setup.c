/*
 * iop13xx platform Initialization
 * Copyright (c) 2005-2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/dma-mapping.h>
#include <linux/serial_8250.h>
#include <linux/io.h>
#ifdef CONFIG_MTD_PHYSMAP
#include <linux/mtd/physmap.h>
#endif
#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/hardware/iop_adma.h>

#define IOP13XX_UART_XTAL 33334000
#define IOP13XX_SETUP_DEBUG 0
#define PRINTK(x...) ((void)(IOP13XX_SETUP_DEBUG && printk(x)))

/* Standard IO mapping for all IOP13XX based systems
 */
static struct map_desc iop13xx_std_desc[] __initdata = {
	{    /* mem mapped registers */
		.virtual = IOP13XX_PMMR_VIRT_MEM_BASE,
		.pfn 	 = __phys_to_pfn(IOP13XX_PMMR_PHYS_MEM_BASE),
		.length  = IOP13XX_PMMR_SIZE,
		.type	 = MT_DEVICE,
	}, { /* PCIE IO space */
		.virtual = IOP13XX_PCIE_LOWER_IO_VA,
		.pfn 	 = __phys_to_pfn(IOP13XX_PCIE_LOWER_IO_PA),
		.length  = IOP13XX_PCIX_IO_WINDOW_SIZE,
		.type	 = MT_DEVICE,
	}, { /* PCIX IO space */
		.virtual = IOP13XX_PCIX_LOWER_IO_VA,
		.pfn 	 = __phys_to_pfn(IOP13XX_PCIX_LOWER_IO_PA),
		.length  = IOP13XX_PCIX_IO_WINDOW_SIZE,
		.type	 = MT_DEVICE,
	},
};

static struct resource iop13xx_uart0_resources[] = {
	[0] = {
		.start = IOP13XX_UART0_PHYS,
		.end = IOP13XX_UART0_PHYS + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_UART0,
		.end = IRQ_IOP13XX_UART0,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_uart1_resources[] = {
	[0] = {
		.start = IOP13XX_UART1_PHYS,
		.end = IOP13XX_UART1_PHYS + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_UART1,
		.end = IRQ_IOP13XX_UART1,
		.flags = IORESOURCE_IRQ
	}
};

static struct plat_serial8250_port iop13xx_uart0_data[] = {
	{
       .membase     = (char*)(IOP13XX_UART0_VIRT),
       .mapbase     = (IOP13XX_UART0_PHYS),
       .irq         = IRQ_IOP13XX_UART0,
       .uartclk     = IOP13XX_UART_XTAL,
       .regshift    = 2,
       .iotype      = UPIO_MEM,
       .flags       = UPF_SKIP_TEST,
	},
	{  },
};

static struct plat_serial8250_port iop13xx_uart1_data[] = {
	{
       .membase     = (char*)(IOP13XX_UART1_VIRT),
       .mapbase     = (IOP13XX_UART1_PHYS),
       .irq         = IRQ_IOP13XX_UART1,
       .uartclk     = IOP13XX_UART_XTAL,
       .regshift    = 2,
       .iotype      = UPIO_MEM,
       .flags       = UPF_SKIP_TEST,
	},
	{  },
};

/* The ids are fixed up later in iop13xx_platform_init */
static struct platform_device iop13xx_uart0 = {
       .name = "serial8250",
       .id = 0,
       .dev.platform_data = iop13xx_uart0_data,
       .num_resources = 2,
       .resource = iop13xx_uart0_resources,
};

static struct platform_device iop13xx_uart1 = {
       .name = "serial8250",
       .id = 0,
       .dev.platform_data = iop13xx_uart1_data,
       .num_resources = 2,
       .resource = iop13xx_uart1_resources
};

static struct resource iop13xx_i2c_0_resources[] = {
	[0] = {
		.start = IOP13XX_I2C0_PHYS,
		.end = IOP13XX_I2C0_PHYS + 0x18,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_I2C_0,
		.end = IRQ_IOP13XX_I2C_0,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_i2c_1_resources[] = {
	[0] = {
		.start = IOP13XX_I2C1_PHYS,
		.end = IOP13XX_I2C1_PHYS + 0x18,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_I2C_1,
		.end = IRQ_IOP13XX_I2C_1,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_i2c_2_resources[] = {
	[0] = {
		.start = IOP13XX_I2C2_PHYS,
		.end = IOP13XX_I2C2_PHYS + 0x18,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_I2C_2,
		.end = IRQ_IOP13XX_I2C_2,
		.flags = IORESOURCE_IRQ
	}
};

/* I2C controllers. The IOP13XX uses the same block as the IOP3xx, so
 * we just use the same device name.
 */

/* The ids are fixed up later in iop13xx_platform_init */
static struct platform_device iop13xx_i2c_0_controller = {
	.name = "IOP3xx-I2C",
	.id = 0,
	.num_resources = 2,
	.resource = iop13xx_i2c_0_resources
};

static struct platform_device iop13xx_i2c_1_controller = {
	.name = "IOP3xx-I2C",
	.id = 0,
	.num_resources = 2,
	.resource = iop13xx_i2c_1_resources
};

static struct platform_device iop13xx_i2c_2_controller = {
	.name = "IOP3xx-I2C",
	.id = 0,
	.num_resources = 2,
	.resource = iop13xx_i2c_2_resources
};

#ifdef CONFIG_MTD_PHYSMAP
/* PBI Flash Device
 */
static struct physmap_flash_data iq8134x_flash_data = {
	.width = 2,
};

static struct resource iq8134x_flash_resource = {
	.start = IQ81340_FLASHBASE,
	.end   = 0,
	.flags = IORESOURCE_MEM,
};

static struct platform_device iq8134x_flash = {
	.name           = "physmap-flash",
	.id             = 0,
	.dev            = { .platform_data  = &iq8134x_flash_data, },
	.num_resources  = 1,
	.resource       = &iq8134x_flash_resource,
};

static unsigned long iq8134x_probe_flash_size(void)
{
	uint8_t __iomem *flash_addr = ioremap(IQ81340_FLASHBASE, PAGE_SIZE);
	int i;
	char query[3];
	unsigned long size = 0;
	int width = iq8134x_flash_data.width;

	if (flash_addr) {
		/* send CFI 'query' command */
		writew(0x98, flash_addr);

		/* check for CFI compliance */
		for (i = 0; i < 3 * width; i += width)
			query[i / width] = readb(flash_addr + (0x10 * width) + i);

		/* read the size */
		if (memcmp(query, "QRY", 3) == 0)
			size = 1 << readb(flash_addr + (0x27 * width));

		/* send CFI 'read array' command */
		writew(0xff, flash_addr);

		iounmap(flash_addr);
	}

	return size;
}
#endif

/* ADMA Channels */
static struct resource iop13xx_adma_0_resources[] = {
	[0] = {
		.start = IOP13XX_ADMA_PHYS_BASE(0),
		.end = IOP13XX_ADMA_UPPER_PA(0),
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_ADMA0_EOT,
		.end = IRQ_IOP13XX_ADMA0_EOT,
		.flags = IORESOURCE_IRQ
	},
	[2] = {
		.start = IRQ_IOP13XX_ADMA0_EOC,
		.end = IRQ_IOP13XX_ADMA0_EOC,
		.flags = IORESOURCE_IRQ
	},
	[3] = {
		.start = IRQ_IOP13XX_ADMA0_ERR,
		.end = IRQ_IOP13XX_ADMA0_ERR,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_adma_1_resources[] = {
	[0] = {
		.start = IOP13XX_ADMA_PHYS_BASE(1),
		.end = IOP13XX_ADMA_UPPER_PA(1),
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_ADMA1_EOT,
		.end = IRQ_IOP13XX_ADMA1_EOT,
		.flags = IORESOURCE_IRQ
	},
	[2] = {
		.start = IRQ_IOP13XX_ADMA1_EOC,
		.end = IRQ_IOP13XX_ADMA1_EOC,
		.flags = IORESOURCE_IRQ
	},
	[3] = {
		.start = IRQ_IOP13XX_ADMA1_ERR,
		.end = IRQ_IOP13XX_ADMA1_ERR,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop13xx_adma_2_resources[] = {
	[0] = {
		.start = IOP13XX_ADMA_PHYS_BASE(2),
		.end = IOP13XX_ADMA_UPPER_PA(2),
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP13XX_ADMA2_EOT,
		.end = IRQ_IOP13XX_ADMA2_EOT,
		.flags = IORESOURCE_IRQ
	},
	[2] = {
		.start = IRQ_IOP13XX_ADMA2_EOC,
		.end = IRQ_IOP13XX_ADMA2_EOC,
		.flags = IORESOURCE_IRQ
	},
	[3] = {
		.start = IRQ_IOP13XX_ADMA2_ERR,
		.end = IRQ_IOP13XX_ADMA2_ERR,
		.flags = IORESOURCE_IRQ
	}
};

static u64 iop13xx_adma_dmamask = DMA_BIT_MASK(64);
static struct iop_adma_platform_data iop13xx_adma_0_data = {
	.hw_id = 0,
	.pool_size = PAGE_SIZE,
};

static struct iop_adma_platform_data iop13xx_adma_1_data = {
	.hw_id = 1,
	.pool_size = PAGE_SIZE,
};

static struct iop_adma_platform_data iop13xx_adma_2_data = {
	.hw_id = 2,
	.pool_size = PAGE_SIZE,
};

/* The ids are fixed up later in iop13xx_platform_init */
static struct platform_device iop13xx_adma_0_channel = {
	.name = "iop-adma",
	.id = 0,
	.num_resources = 4,
	.resource = iop13xx_adma_0_resources,
	.dev = {
		.dma_mask = &iop13xx_adma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(64),
		.platform_data = (void *) &iop13xx_adma_0_data,
	},
};

static struct platform_device iop13xx_adma_1_channel = {
	.name = "iop-adma",
	.id = 0,
	.num_resources = 4,
	.resource = iop13xx_adma_1_resources,
	.dev = {
		.dma_mask = &iop13xx_adma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(64),
		.platform_data = (void *) &iop13xx_adma_1_data,
	},
};

static struct platform_device iop13xx_adma_2_channel = {
	.name = "iop-adma",
	.id = 0,
	.num_resources = 4,
	.resource = iop13xx_adma_2_resources,
	.dev = {
		.dma_mask = &iop13xx_adma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(64),
		.platform_data = (void *) &iop13xx_adma_2_data,
	},
};

void __init iop13xx_map_io(void)
{
	/* Initialize the Static Page Table maps */
	iotable_init(iop13xx_std_desc, ARRAY_SIZE(iop13xx_std_desc));
}

static int init_uart;
static int init_i2c;
static int init_adma;

void __init iop13xx_platform_init(void)
{
	int i;
	u32 uart_idx, i2c_idx, adma_idx, plat_idx;
	struct platform_device *iop13xx_devices[IQ81340_MAX_PLAT_DEVICES];

	/* set the bases so we can read the device id */
	iop13xx_set_atu_mmr_bases();

	memset(iop13xx_devices, 0, sizeof(iop13xx_devices));

	if (init_uart == IOP13XX_INIT_UART_DEFAULT) {
		switch (iop13xx_dev_id()) {
		/* enable both uarts on iop341 */
		case 0x3380:
		case 0x3384:
		case 0x3388:
		case 0x338c:
			init_uart |= IOP13XX_INIT_UART_0;
			init_uart |= IOP13XX_INIT_UART_1;
			break;
		/* only enable uart 1 */
		default:
			init_uart |= IOP13XX_INIT_UART_1;
		}
	}

	if (init_i2c == IOP13XX_INIT_I2C_DEFAULT) {
		switch (iop13xx_dev_id()) {
		/* enable all i2c units on iop341 and iop342 */
		case 0x3380:
		case 0x3384:
		case 0x3388:
		case 0x338c:
		case 0x3382:
		case 0x3386:
		case 0x338a:
		case 0x338e:
			init_i2c |= IOP13XX_INIT_I2C_0;
			init_i2c |= IOP13XX_INIT_I2C_1;
			init_i2c |= IOP13XX_INIT_I2C_2;
			break;
		/* only enable i2c 1 and 2 */
		default:
			init_i2c |= IOP13XX_INIT_I2C_1;
			init_i2c |= IOP13XX_INIT_I2C_2;
		}
	}

	if (init_adma == IOP13XX_INIT_ADMA_DEFAULT) {
		init_adma |= IOP13XX_INIT_ADMA_0;
		init_adma |= IOP13XX_INIT_ADMA_1;
		init_adma |= IOP13XX_INIT_ADMA_2;
	}

	plat_idx = 0;
	uart_idx = 0;
	i2c_idx = 0;

	/* uart 1 (if enabled) is ttyS0 */
	if (init_uart & IOP13XX_INIT_UART_1) {
		PRINTK("Adding uart1 to platform device list\n");
		iop13xx_uart1.id = uart_idx++;
		iop13xx_devices[plat_idx++] = &iop13xx_uart1;
	}
	if (init_uart & IOP13XX_INIT_UART_0) {
		PRINTK("Adding uart0 to platform device list\n");
		iop13xx_uart0.id = uart_idx++;
		iop13xx_devices[plat_idx++] = &iop13xx_uart0;
	}

	for(i = 0; i < IQ81340_NUM_I2C; i++) {
		if ((init_i2c & (1 << i)) && IOP13XX_SETUP_DEBUG)
			printk("Adding i2c%d to platform device list\n", i);
		switch(init_i2c & (1 << i)) {
		case IOP13XX_INIT_I2C_0:
			iop13xx_i2c_0_controller.id = i2c_idx++;
			iop13xx_devices[plat_idx++] =
				&iop13xx_i2c_0_controller;
			break;
		case IOP13XX_INIT_I2C_1:
			iop13xx_i2c_1_controller.id = i2c_idx++;
			iop13xx_devices[plat_idx++] =
				&iop13xx_i2c_1_controller;
			break;
		case IOP13XX_INIT_I2C_2:
			iop13xx_i2c_2_controller.id = i2c_idx++;
			iop13xx_devices[plat_idx++] =
				&iop13xx_i2c_2_controller;
			break;
		}
	}

	/* initialize adma channel ids and capabilities */
	adma_idx = 0;
	for (i = 0; i < IQ81340_NUM_ADMA; i++) {
		struct iop_adma_platform_data *plat_data;
		if ((init_adma & (1 << i)) && IOP13XX_SETUP_DEBUG)
			printk(KERN_INFO
				"Adding adma%d to platform device list\n", i);
		switch (init_adma & (1 << i)) {
		case IOP13XX_INIT_ADMA_0:
			iop13xx_adma_0_channel.id = adma_idx++;
			iop13xx_devices[plat_idx++] = &iop13xx_adma_0_channel;
			plat_data = &iop13xx_adma_0_data;
			dma_cap_set(DMA_MEMCPY, plat_data->cap_mask);
			dma_cap_set(DMA_XOR, plat_data->cap_mask);
			dma_cap_set(DMA_XOR_VAL, plat_data->cap_mask);
			dma_cap_set(DMA_MEMSET, plat_data->cap_mask);
			dma_cap_set(DMA_INTERRUPT, plat_data->cap_mask);
			break;
		case IOP13XX_INIT_ADMA_1:
			iop13xx_adma_1_channel.id = adma_idx++;
			iop13xx_devices[plat_idx++] = &iop13xx_adma_1_channel;
			plat_data = &iop13xx_adma_1_data;
			dma_cap_set(DMA_MEMCPY, plat_data->cap_mask);
			dma_cap_set(DMA_XOR, plat_data->cap_mask);
			dma_cap_set(DMA_XOR_VAL, plat_data->cap_mask);
			dma_cap_set(DMA_MEMSET, plat_data->cap_mask);
			dma_cap_set(DMA_INTERRUPT, plat_data->cap_mask);
			break;
		case IOP13XX_INIT_ADMA_2:
			iop13xx_adma_2_channel.id = adma_idx++;
			iop13xx_devices[plat_idx++] = &iop13xx_adma_2_channel;
			plat_data = &iop13xx_adma_2_data;
			dma_cap_set(DMA_MEMCPY, plat_data->cap_mask);
			dma_cap_set(DMA_XOR, plat_data->cap_mask);
			dma_cap_set(DMA_XOR_VAL, plat_data->cap_mask);
			dma_cap_set(DMA_MEMSET, plat_data->cap_mask);
			dma_cap_set(DMA_INTERRUPT, plat_data->cap_mask);
			dma_cap_set(DMA_PQ, plat_data->cap_mask);
			dma_cap_set(DMA_PQ_VAL, plat_data->cap_mask);
			break;
		}
	}

#ifdef CONFIG_MTD_PHYSMAP
	iq8134x_flash_resource.end = iq8134x_flash_resource.start +
				iq8134x_probe_flash_size() - 1;
	if (iq8134x_flash_resource.end > iq8134x_flash_resource.start)
		iop13xx_devices[plat_idx++] = &iq8134x_flash;
	else
		printk(KERN_ERR "%s: Failed to probe flash size\n", __func__);
#endif

	platform_add_devices(iop13xx_devices, plat_idx);
}

static int __init iop13xx_init_uart_setup(char *str)
{
	if (str) {
		while (*str != '\0') {
			switch(*str) {
			case '0':
				init_uart |= IOP13XX_INIT_UART_0;
				break;
			case '1':
				init_uart |= IOP13XX_INIT_UART_1;
				break;
			case ',':
			case '=':
				break;
			default:
				PRINTK("\"iop13xx_init_uart\" malformed"
					    " at character: \'%c\'", *str);
				*(str + 1) = '\0';
				init_uart = IOP13XX_INIT_UART_DEFAULT;
			}
			str++;
		}
	}
	return 1;
}

static int __init iop13xx_init_i2c_setup(char *str)
{
	if (str) {
		while (*str != '\0') {
			switch(*str) {
			case '0':
				init_i2c |= IOP13XX_INIT_I2C_0;
				break;
			case '1':
				init_i2c |= IOP13XX_INIT_I2C_1;
				break;
			case '2':
				init_i2c |= IOP13XX_INIT_I2C_2;
				break;
			case ',':
			case '=':
				break;
			default:
				PRINTK("\"iop13xx_init_i2c\" malformed"
					    " at character: \'%c\'", *str);
				*(str + 1) = '\0';
				init_i2c = IOP13XX_INIT_I2C_DEFAULT;
			}
			str++;
		}
	}
	return 1;
}

static int __init iop13xx_init_adma_setup(char *str)
{
	if (str)	{
		while (*str != '\0') {
			switch (*str) {
			case '0':
				init_adma |= IOP13XX_INIT_ADMA_0;
				break;
			case '1':
				init_adma |= IOP13XX_INIT_ADMA_1;
				break;
			case '2':
				init_adma |= IOP13XX_INIT_ADMA_2;
				break;
			case ',':
			case '=':
				break;
			default:
				PRINTK("\"iop13xx_init_adma\" malformed"
					    " at character: \'%c\'", *str);
				*(str + 1) = '\0';
				init_adma = IOP13XX_INIT_ADMA_DEFAULT;
			}
			str++;
		}
	}
	return 1;
}

__setup("iop13xx_init_adma", iop13xx_init_adma_setup);
__setup("iop13xx_init_uart", iop13xx_init_uart_setup);
__setup("iop13xx_init_i2c", iop13xx_init_i2c_setup);
