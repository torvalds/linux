// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/boards/mach-x3proto/setup.c
 *
 * Renesas SH-X3 Prototype Board Support.
 *
 * Copyright (C) 2007 - 2010  Paul Mundt
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/smc91x.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/usb/r8a66597.h>
#include <linux/usb/m66592.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <mach/ilsel.h>
#include <mach/hardware.h>
#include <asm/smp-ops.h>

static struct resource heartbeat_resources[] = {
	[0] = {
		.start	= 0xb8140020,
		.end	= 0xb8140020,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device heartbeat_device = {
	.name		= "heartbeat",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(heartbeat_resources),
	.resource	= heartbeat_resources,
};

static struct smc91x_platdata smc91x_info = {
	.flags	= SMC91X_USE_16BIT | SMC91X_NOWAIT,
};

static struct resource smc91x_resources[] = {
	[0] = {
		.start		= 0x18000300,
		.end		= 0x18000300 + 0x10 - 1,
		.flags		= IORESOURCE_MEM,
	},
	[1] = {
		/* Filled in by ilsel */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= -1,
	.resource	= smc91x_resources,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.dev	= {
		.platform_data = &smc91x_info,
	},
};

static struct r8a66597_platdata r8a66597_data = {
	.xtal = R8A66597_PLATDATA_XTAL_12MHZ,
	.vif = 1,
};

static struct resource r8a66597_usb_host_resources[] = {
	[0] = {
		.start	= 0x18040000,
		.end	= 0x18080000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		/* Filled in by ilsel */
		.flags	= IORESOURCE_IRQ | IRQF_TRIGGER_LOW,
	},
};

static struct platform_device r8a66597_usb_host_device = {
	.name		= "r8a66597_hcd",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,		/* don't use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &r8a66597_data,
	},
	.num_resources	= ARRAY_SIZE(r8a66597_usb_host_resources),
	.resource	= r8a66597_usb_host_resources,
};

static struct m66592_platdata usbf_platdata = {
	.xtal = M66592_PLATDATA_XTAL_24MHZ,
	.vif = 1,
};

static struct resource m66592_usb_peripheral_resources[] = {
	[0] = {
		.name	= "m66592_udc",
		.start	= 0x18080000,
		.end	= 0x180c0000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.name	= "m66592_udc",
		/* Filled in by ilsel */
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device m66592_usb_peripheral_device = {
	.name		= "m66592_udc",
	.id		= -1,
	.dev = {
		.dma_mask		= NULL,		/* don't use dma */
		.coherent_dma_mask	= 0xffffffff,
		.platform_data		= &usbf_platdata,
	},
	.num_resources	= ARRAY_SIZE(m66592_usb_peripheral_resources),
	.resource	= m66592_usb_peripheral_resources,
};

static struct gpio_keys_button baseboard_buttons[NR_BASEBOARD_GPIOS] = {
	{
		.desc		= "key44",
		.code		= KEY_POWER,
		.active_low	= 1,
		.wakeup		= 1,
	}, {
		.desc		= "key43",
		.code		= KEY_SUSPEND,
		.active_low	= 1,
		.wakeup		= 1,
	}, {
		.desc		= "key42",
		.code		= KEY_KATAKANAHIRAGANA,
		.active_low	= 1,
	}, {
		.desc		= "key41",
		.code		= KEY_SWITCHVIDEOMODE,
		.active_low	= 1,
	}, {
		.desc		= "key34",
		.code		= KEY_F12,
		.active_low	= 1,
	}, {
		.desc		= "key33",
		.code		= KEY_F11,
		.active_low	= 1,
	}, {
		.desc		= "key32",
		.code		= KEY_F10,
		.active_low	= 1,
	}, {
		.desc		= "key31",
		.code		= KEY_F9,
		.active_low	= 1,
	}, {
		.desc		= "key24",
		.code		= KEY_F8,
		.active_low	= 1,
	}, {
		.desc		= "key23",
		.code		= KEY_F7,
		.active_low	= 1,
	}, {
		.desc		= "key22",
		.code		= KEY_F6,
		.active_low	= 1,
	}, {
		.desc		= "key21",
		.code		= KEY_F5,
		.active_low	= 1,
	}, {
		.desc		= "key14",
		.code		= KEY_F4,
		.active_low	= 1,
	}, {
		.desc		= "key13",
		.code		= KEY_F3,
		.active_low	= 1,
	}, {
		.desc		= "key12",
		.code		= KEY_F2,
		.active_low	= 1,
	}, {
		.desc		= "key11",
		.code		= KEY_F1,
		.active_low	= 1,
	},
};

static struct gpio_keys_platform_data baseboard_buttons_data = {
	.buttons	= baseboard_buttons,
	.nbuttons	= ARRAY_SIZE(baseboard_buttons),
};

static struct platform_device baseboard_buttons_device = {
	.name		= "gpio-keys",
	.id		= -1,
	.dev		= {
		.platform_data	= &baseboard_buttons_data,
	},
};

static struct platform_device *x3proto_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_device,
	&r8a66597_usb_host_device,
	&m66592_usb_peripheral_device,
	&baseboard_buttons_device,
};

static void __init x3proto_init_irq(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRL3210);

	/* Set ICR0.LVLMODE */
	__raw_writel(__raw_readl(0xfe410000) | (1 << 21), 0xfe410000);
}

static int __init x3proto_devices_setup(void)
{
	int ret, i;

	/*
	 * IRLs are only needed for ILSEL mappings, so flip over the INTC
	 * pins at a later point to enable the GPIOs to settle.
	 */
	x3proto_init_irq();

	/*
	 * Now that ILSELs are available, set up the baseboard GPIOs.
	 */
	ret = x3proto_gpio_setup();
	if (unlikely(ret))
		return ret;

	/*
	 * Propagate dynamic GPIOs for the baseboard button device.
	 */
	for (i = 0; i < ARRAY_SIZE(baseboard_buttons); i++)
		baseboard_buttons[i].gpio = x3proto_gpio_chip.base + i;

	r8a66597_usb_host_resources[1].start =
		r8a66597_usb_host_resources[1].end = ilsel_enable(ILSEL_USBH_I);

	m66592_usb_peripheral_resources[1].start =
		m66592_usb_peripheral_resources[1].end = ilsel_enable(ILSEL_USBP_I);

	smc91x_resources[1].start =
		smc91x_resources[1].end = ilsel_enable(ILSEL_LAN);

	return platform_add_devices(x3proto_devices,
				    ARRAY_SIZE(x3proto_devices));
}
device_initcall(x3proto_devices_setup);

static void __init x3proto_setup(char **cmdline_p)
{
	register_smp_ops(&shx3_smp_ops);
}

static struct sh_machine_vector mv_x3proto __initmv = {
	.mv_name		= "x3proto",
	.mv_setup		= x3proto_setup,
};
