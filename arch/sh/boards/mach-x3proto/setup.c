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
#include <linux/gpio/machine.h>
#include <linux/gpio/property.h>
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

static const struct software_node x3proto_gpio_keys_node = {
	.name = "x3proto-gpio-keys",
};

#define __X3PROTO_KEY(_id, _code, _gpio, ...)				\
static const struct property_entry x3proto_key##_id##_props[] = {	\
	PROPERTY_ENTRY_STRING("label", "key" #_id),			\
	PROPERTY_ENTRY_U32("linux,code", _code),			\
	PROPERTY_ENTRY_GPIO("gpios", &x3proto_gpiochip_node,		\
			    _gpio, GPIO_ACTIVE_LOW),			\
	__VA_ARGS__							\
	{ }								\
};									\
static const struct software_node x3proto_key##_id##_node = {		\
	.parent = &x3proto_gpio_keys_node,				\
	.properties = x3proto_key##_id##_props,				\
}

#define X3PROTO_KEY(_id, _code, _gpio)					\
	__X3PROTO_KEY(_id, _code, _gpio)

#define X3PROTO_KEY_WAKEUP(_id, _code, _gpio)				\
	__X3PROTO_KEY(_id, _code, _gpio,				\
		      PROPERTY_ENTRY_BOOL("wakeup-source"),		\
	)

X3PROTO_KEY_WAKEUP(44, KEY_POWER, 0);
X3PROTO_KEY_WAKEUP(43, KEY_SUSPEND, 1);
X3PROTO_KEY(42, KEY_KATAKANAHIRAGANA, 2);
X3PROTO_KEY(41, KEY_SWITCHVIDEOMODE, 3);
X3PROTO_KEY(34, KEY_F12, 4);
X3PROTO_KEY(33, KEY_F11, 5);
X3PROTO_KEY(32, KEY_F10, 6);
X3PROTO_KEY(31, KEY_F9, 7);
X3PROTO_KEY(24, KEY_F8, 8);
X3PROTO_KEY(23, KEY_F7, 9);
X3PROTO_KEY(22, KEY_F6, 10);
X3PROTO_KEY(21, KEY_F5, 11);
X3PROTO_KEY(14, KEY_F4, 12);
X3PROTO_KEY(13, KEY_F3, 13);
X3PROTO_KEY(12, KEY_F2, 14);
X3PROTO_KEY(11, KEY_F1, 15);

static const struct software_node *const x3proto_swnodes[] __initconst = {
	&x3proto_gpio_keys_node,
	&x3proto_key44_node,
	&x3proto_key43_node,
	&x3proto_key42_node,
	&x3proto_key41_node,
	&x3proto_key34_node,
	&x3proto_key33_node,
	&x3proto_key32_node,
	&x3proto_key31_node,
	&x3proto_key24_node,
	&x3proto_key23_node,
	&x3proto_key22_node,
	&x3proto_key21_node,
	&x3proto_key14_node,
	&x3proto_key13_node,
	&x3proto_key12_node,
	&x3proto_key11_node,
	NULL
};

static const struct platform_device_info x3proto_gpio_keys_device_info __initconst = {
	.name		= "gpio-keys",
	.id		= PLATFORM_DEVID_NONE,
	.swnode		= &x3proto_gpio_keys_node,
};

static struct platform_device *x3proto_devices[] __initdata = {
	&heartbeat_device,
	&smc91x_device,
	&r8a66597_usb_host_device,
	&m66592_usb_peripheral_device,
};

static void __init x3proto_init_irq(void)
{
	plat_irq_setup_pins(IRQ_MODE_IRL3210);

	/* Set ICR0.LVLMODE */
	__raw_writel(__raw_readl(0xfe410000) | (1 << 21), 0xfe410000);
}

static int __init x3proto_devices_setup(void)
{
	struct platform_device *pd;
	int ret;

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

	ret = software_node_register_node_group(x3proto_swnodes);
	if (ret) {
		pr_err("Failed to register software nodes: %d\n", ret);
		return ret;
	}

	r8a66597_usb_host_resources[1].start =
		r8a66597_usb_host_resources[1].end = ilsel_enable(ILSEL_USBH_I);

	m66592_usb_peripheral_resources[1].start =
		m66592_usb_peripheral_resources[1].end = ilsel_enable(ILSEL_USBP_I);

	smc91x_resources[1].start =
		smc91x_resources[1].end = ilsel_enable(ILSEL_LAN);

	ret = platform_add_devices(x3proto_devices, ARRAY_SIZE(x3proto_devices));
	if (ret)
		return ret;

	pd = platform_device_register_full(&x3proto_gpio_keys_device_info);
	return PTR_ERR_OR_ZERO(pd);
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
