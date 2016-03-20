/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2007 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/rtc/ds1685.h>

#include <asm/ip32/mace.h>
#include <asm/ip32/ip32_ints.h>

extern void ip32_prepare_poweroff(void);

#define MACEISA_SERIAL1_OFFS   offsetof(struct sgi_mace, isa.serial1)
#define MACEISA_SERIAL2_OFFS   offsetof(struct sgi_mace, isa.serial2)

#define MACE_PORT(offset,_irq)						\
{									\
	.mapbase	= MACE_BASE + offset,				\
	.irq		= _irq,						\
	.uartclk	= 1843200,					\
	.iotype		= UPIO_MEM,					\
	.flags		= UPF_SKIP_TEST|UPF_IOREMAP,			\
	.regshift	= 8,						\
}

static struct plat_serial8250_port uart8250_data[] = {
	MACE_PORT(MACEISA_SERIAL1_OFFS, MACEISA_SERIAL1_IRQ),
	MACE_PORT(MACEISA_SERIAL2_OFFS, MACEISA_SERIAL2_IRQ),
	{ },
};

static struct platform_device uart8250_device = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
	.dev			= {
		.platform_data	= uart8250_data,
	},
};

static int __init uart8250_init(void)
{
	return platform_device_register(&uart8250_device);
}

device_initcall(uart8250_init);

static __init int meth_devinit(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("meth", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}

device_initcall(meth_devinit);

static __init int sgio2audio_devinit(void)
{
	struct platform_device *pd;
	int ret;

	pd = platform_device_alloc("sgio2audio", -1);
	if (!pd)
		return -ENOMEM;

	ret = platform_device_add(pd);
	if (ret)
		platform_device_put(pd);

	return ret;
}

device_initcall(sgio2audio_devinit);

static __init int sgio2btns_devinit(void)
{
	return IS_ERR(platform_device_register_simple("sgibtns", -1, NULL, 0));
}

device_initcall(sgio2btns_devinit);

#define MACE_RTC_RES_START (MACE_BASE + offsetof(struct sgi_mace, isa.rtc))
#define MACE_RTC_RES_END (MACE_RTC_RES_START + 32767)

static struct resource ip32_rtc_resources[] = {
	{
		.start	= MACEISA_RTC_IRQ,
		.end	= MACEISA_RTC_IRQ,
		.flags	= IORESOURCE_IRQ
	}, {
		.start	= MACE_RTC_RES_START,
		.end	= MACE_RTC_RES_END,
		.flags	= IORESOURCE_MEM,
	}
};

/* RTC registers on IP32 are each padded by 256 bytes (0x100). */
static struct ds1685_rtc_platform_data
ip32_rtc_platform_data[] = {
	{
		.regstep = 0x100,
		.bcd_mode = true,
		.no_irq = false,
		.uie_unsupported = false,
		.alloc_io_resources = true,
		.plat_prepare_poweroff = ip32_prepare_poweroff,
	},
};

struct platform_device ip32_rtc_device = {
	.name			= "rtc-ds1685",
	.id			= -1,
	.dev			= {
		.platform_data	= ip32_rtc_platform_data,
	},
	.num_resources		= ARRAY_SIZE(ip32_rtc_resources),
	.resource		= ip32_rtc_resources,
};

static __init int sgio2_rtc_devinit(void)
{
	return platform_device_register(&ip32_rtc_device);
}

device_initcall(sgio2_rtc_devinit);
