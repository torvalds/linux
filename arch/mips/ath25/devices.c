#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/serial_8250.h>
#include <linux/platform_device.h>
#include <asm/bootinfo.h>

#include <ath25_platform.h>
#include "devices.h"
#include "ar5312.h"
#include "ar2315.h"

struct ar231x_board_config ath25_board;
enum ath25_soc_type ath25_soc = ATH25_SOC_UNKNOWN;

static struct resource ath25_wmac0_res[] = {
	{
		.name = "wmac0_membase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "wmac0_irq",
		.flags = IORESOURCE_IRQ,
	}
};

static struct resource ath25_wmac1_res[] = {
	{
		.name = "wmac1_membase",
		.flags = IORESOURCE_MEM,
	},
	{
		.name = "wmac1_irq",
		.flags = IORESOURCE_IRQ,
	}
};

static struct platform_device ath25_wmac[] = {
	{
		.id = 0,
		.name = "ar231x-wmac",
		.resource = ath25_wmac0_res,
		.num_resources = ARRAY_SIZE(ath25_wmac0_res),
		.dev.platform_data = &ath25_board,
	},
	{
		.id = 1,
		.name = "ar231x-wmac",
		.resource = ath25_wmac1_res,
		.num_resources = ARRAY_SIZE(ath25_wmac1_res),
		.dev.platform_data = &ath25_board,
	},
};

static const char * const soc_type_strings[] = {
	[ATH25_SOC_AR5312] = "Atheros AR5312",
	[ATH25_SOC_AR2312] = "Atheros AR2312",
	[ATH25_SOC_AR2313] = "Atheros AR2313",
	[ATH25_SOC_AR2315] = "Atheros AR2315",
	[ATH25_SOC_AR2316] = "Atheros AR2316",
	[ATH25_SOC_AR2317] = "Atheros AR2317",
	[ATH25_SOC_AR2318] = "Atheros AR2318",
	[ATH25_SOC_UNKNOWN] = "Atheros (unknown)",
};

const char *get_system_type(void)
{
	if ((ath25_soc >= ARRAY_SIZE(soc_type_strings)) ||
	    !soc_type_strings[ath25_soc])
		return soc_type_strings[ATH25_SOC_UNKNOWN];
	return soc_type_strings[ath25_soc];
}

void __init ath25_serial_setup(u32 mapbase, int irq, unsigned int uartclk)
{
	struct uart_port s;

	memset(&s, 0, sizeof(s));

	s.flags = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_IOREMAP;
	s.iotype = UPIO_MEM32;
	s.irq = irq;
	s.regshift = 2;
	s.mapbase = mapbase;
	s.uartclk = uartclk;

	early_serial_setup(&s);
}

int __init ath25_add_wmac(int nr, u32 base, int irq)
{
	struct resource *res;

	ath25_wmac[nr].dev.platform_data = &ath25_board;
	res = &ath25_wmac[nr].resource[0];
	res->start = base;
	res->end = base + 0x10000 - 1;
	res++;
	res->start = irq;
	res->end = irq;
	return platform_device_register(&ath25_wmac[nr]);
}

static int __init ath25_register_devices(void)
{
	if (is_ar5312())
		ar5312_init_devices();
	else
		ar2315_init_devices();

	return 0;
}

device_initcall(ath25_register_devices);

static int __init ath25_arch_init(void)
{
	if (is_ar5312())
		ar5312_arch_init();
	else
		ar2315_arch_init();

	return 0;
}

arch_initcall(ath25_arch_init);
